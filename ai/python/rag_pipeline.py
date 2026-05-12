"""
ai/python/rag_pipeline.py
=========================

Retrieval-Augmented Generation (RAG) pipeline for POWSYS365.

Provides end-to-end document ingestion, embedding, vector storage,
and semantic retrieval for powering LLM-based analysis with
grounded technical context.

Pipeline stages:
    DocumentLoader -> TextSplitter -> Embedding -> VectorStore -> Retriever -> RAGChain

Storage backends:
    - InMemoryVectorStore (default, no external dependencies)
    - PgVectorStore (PostgreSQL + pgvector for production)
"""

from __future__ import annotations

import hashlib
import json
import logging
import os
import re
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

import numpy as np

logger = logging.getLogger(__name__)


# =========================================================================
# Document model
# =========================================================================


@dataclass
class Document:
    """A loaded document with content and metadata."""

    source: str  # File path or URL
    content: str
    title: str = ""
    doc_type: str = ""  # "pdf", "txt", "md", etc.
    metadata: dict[str, Any] = field(default_factory=dict)

    @property
    def doc_id(self) -> str:
        """Unique document identifier based on source path hash."""
        return hashlib.sha256(self.source.encode()).hexdigest()[:16]


@dataclass
class DocumentChunk:
    """A text chunk with embedding and provenance."""

    chunk_id: str
    doc_id: str
    source: str
    content: str
    title: str = ""
    chunk_index: int = 0
    total_chunks: int = 1
    embedding: list[float] = field(default_factory=list, repr=False)
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass
class RetrievedChunk:
    """A chunk returned from semantic search."""

    chunk: DocumentChunk
    similarity: float
    rank: int


# =========================================================================
# DocumentLoader
# =========================================================================


class DocumentLoader:
    """Load documents from various file formats.

    Supported formats:
    - Plain text (``.txt``)
    - Markdown (``.md``)
    - PDF (``.pdf``, requires ``pymupdf`` or ``PyPDF2``)
    - CSV / JSON (structured data)
    """

    SUPPORTED_EXTENSIONS = {".txt", ".md", ".markdown", ".pdf", ".csv", ".json"}

    def load(self, path: str | Path) -> Document:
        """Load a single document from *path*.

        Parameters
        ----------
        path: File path to load.

        Returns
        -------
        Document
            Loaded document with extracted text content.

        Raises
        ------
        FileNotFoundError: If the file does not exist.
        ValueError: If the file format is not supported.
        """
        path = Path(path)
        if not path.exists():
            raise FileNotFoundError(f"Document not found: {path}")

        ext = path.suffix.lower()
        if ext not in self.SUPPORTED_EXTENSIONS:
            raise ValueError(
                f"Unsupported file format: {ext}. "
                f"Supported: {self.SUPPORTED_EXTENSIONS}"
            )

        if ext == ".pdf":
            return self._load_pdf(path)
        elif ext == ".csv":
            return self._load_csv(path)
        elif ext == ".json":
            return self._load_json(path)
        else:
            return self._load_text(path)

    def load_directory(
        self,
        directory: str | Path,
        pattern: str = "*",
    ) -> list[Document]:
        """Load all matching documents from a directory.

        Parameters
        ----------
        directory: Directory to scan.
        pattern: Glob pattern for file matching (default ``"*"``).

        Returns
        -------
        list[Document]
            All successfully loaded documents.
        """
        directory = Path(directory)
        if not directory.is_dir():
            raise NotADirectoryError(f"Not a directory: {directory}")

        docs: list[Document] = []
        for ext in self.SUPPORTED_EXTENSIONS:
            for filepath in directory.rglob(f"*{ext}"):
                try:
                    docs.append(self.load(filepath))
                except Exception as exc:
                    logger.warning("Failed to load %s: %s", filepath, exc)
        return docs

    def _load_text(self, path: Path) -> Document:
        content = path.read_text(encoding="utf-8", errors="replace")
        doc_type = ".md" if path.suffix.lower() in {".md", ".markdown"} else ".txt"
        return Document(
            source=str(path),
            content=content,
            title=path.stem,
            doc_type=doc_type,
        )

    def _load_pdf(self, path: Path) -> Document:
        content_parts: list[str] = []

        # Try PyMuPDF (fitz) first - best quality
        try:
            import fitz  # type: ignore[import-untyped]
            doc = fitz.open(str(path))
            for page in doc:
                content_parts.append(page.get_text())
            doc.close()
        except ImportError:
            # Fallback to PyPDF2
            try:
                import PyPDF2  # type: ignore[import-untyped]
                with path.open("rb") as fh:
                    reader = PyPDF2.PdfReader(fh)
                    for page in reader.pages:
                        content_parts.append(page.extract_text() or "")
            except ImportError:
                raise ImportError(
                    "PDF loading requires 'pymupdf' or 'PyPDF2'. "
                    "Install with: pip install pymupdf"
                )

        return Document(
            source=str(path),
            content="\n".join(content_parts),
            title=path.stem,
            doc_type=".pdf",
        )

    def _load_csv(self, path: Path) -> Document:
        import csv

        rows: list[str] = []
        with path.open("r", encoding="utf-8") as fh:
            reader = csv.DictReader(fh)
            for row in reader:
                rows.append(" | ".join(f"{k}: {v}" for k, v in row.items()))

        return Document(
            source=str(path),
            content="\n".join(rows),
            title=path.stem,
            doc_type=".csv",
        )

    def _load_json(self, path: Path) -> Document:
        with path.open("r", encoding="utf-8") as fh:
            data = json.load(fh)
        # Flatten JSON to text for embedding
        content = json.dumps(data, indent=2, ensure_ascii=False)
        return Document(
            source=str(path),
            content=content,
            title=path.stem,
            doc_type=".json",
        )


# =========================================================================
# TextSplitter
# =========================================================================


class TextSplitter:
    """Split documents into overlapping chunks for embedding.

    Parameters
    ----------
    chunk_size: Target chunk size in characters.
    chunk_overlap: Overlap between consecutive chunks.
    separators: Ordered list of separators to try.
    """

    def __init__(
        self,
        chunk_size: int = 1000,
        chunk_overlap: int = 200,
        separators: list[str] | None = None,
    ) -> None:
        self.chunk_size = chunk_size
        self.chunk_overlap = chunk_overlap
        self.separators = separators or ["\n\n", "\n", ". ", " ", ""]

    def split(self, text: str) -> list[str]:
        """Split *text* into overlapping chunks.

        Uses a recursive character-based splitting strategy that tries
        each separator in order, falling back to character splitting
        if no separators produce valid chunks.

        Returns
        -------
        list[str]
            List of text chunks.
        """
        if len(text) <= self.chunk_size:
            return [text] if text else []

        chunks: list[str] = []
        self._split_recursive(text, self.separators.copy(), chunks)
        return chunks

    def split_document(self, doc: Document) -> list[DocumentChunk]:
        """Split a :class:`Document` into :class:`DocumentChunk` objects."""
        texts = self.split(doc.content)
        chunks: list[DocumentChunk] = []

        for i, text in enumerate(texts):
            chunk = DocumentChunk(
                chunk_id=f"{doc.doc_id}_{i}",
                doc_id=doc.doc_id,
                source=doc.source,
                content=text,
                title=doc.title,
                chunk_index=i,
                total_chunks=len(texts),
                metadata=doc.metadata.copy(),
            )
            chunks.append(chunk)

        return chunks

    def _split_recursive(
        self, text: str, separators: list[str], chunks: list[str]
    ) -> None:
        """Recursively split text trying each separator."""
        if not text:
            return

        if len(text) <= self.chunk_size:
            chunks.append(text)
            return

        if not separators:
            # Final fallback: character-level split
            self._character_split(text, chunks)
            return

        separator = separators[0]
        remaining = separators[1:]

        if separator:
            parts = text.split(separator)
        else:
            # Empty separator: character split
            self._character_split(text, chunks)
            return

        # Merge parts into chunks of target size
        current = ""
        for part in parts:
            candidate = current + separator + part if current else part
            if len(candidate) <= self.chunk_size:
                current = candidate
            else:
                if current:
                    chunks.append(current)
                    # Apply overlap
                    overlap_start = max(0, len(current) - self.chunk_overlap)
                    current = current[overlap_start:] + separator + part
                    # If still too long, split further
                    if len(current) > self.chunk_size:
                        self._split_recursive(current, remaining, chunks)
                        current = ""
                else:
                    # Single part too long - recurse with next separator
                    self._split_recursive(part, remaining, chunks)

        if current:
            if len(current) <= self.chunk_size:
                chunks.append(current)
            else:
                self._split_recursive(current, remaining, chunks)

    def _character_split(self, text: str, chunks: list[str]) -> None:
        """Split text at character level with overlap."""
        start = 0
        while start < len(text):
            end = min(start + self.chunk_size, len(text))
            chunks.append(text[start:end])
            start = end - self.chunk_overlap
            if start >= end:
                break


# =========================================================================
# Embedding provider
# =========================================================================


class EmbeddingProvider(ABC):
    """Abstract base for text embedding providers."""

    @abstractmethod
    def embed(self, texts: list[str]) -> list[list[float]]:
        """Compute embeddings for a list of texts.

        Parameters
        ----------
        texts: List of input text strings.

        Returns
        -------
        list[list[float]]
            One embedding vector per input text.
        """

    @abstractmethod
    def dimension(self) -> int:
        """Return the dimensionality of the embedding vectors."""

    @property
    @abstractmethod
    def name(self) -> str:
        """Provider name."""


class SimpleEmbeddingProvider(EmbeddingProvider):
    """A simple deterministic embedding provider using character n-gram hashing.

    This is a **fallback** for when no external embedding service is
    available.  It produces consistent but low-quality embeddings.
    For production use, replace with a proper embedding model
    (OpenAI, sentence-transformers, etc.).
    """

    def __init__(self, dimension: int = 384) -> None:
        self._dimension = dimension

    def embed(self, texts: list[str]) -> list[list[float]]:
        embeddings: list[list[float]] = []
        for text in texts:
            vec = np.zeros(self._dimension, dtype=np.float32)
            # Character-level hash-based embedding
            for i, char in enumerate(text):
                for d in range(self._dimension):
                    idx = (ord(char) * (d + 1) + i * 31) % self._dimension
                    vec[idx] += np.sin(ord(char) + d * 0.1) * 0.01
            # Normalise
            norm = np.linalg.norm(vec)
            if norm > 0:
                vec /= norm
            embeddings.append(vec.tolist())
        return embeddings

    def dimension(self) -> int:
        return self._dimension

    @property
    def name(self) -> str:
        return "SimpleHashEmbedding"


class OpenAIEmbeddingProvider(EmbeddingProvider):
    """OpenAI text-embedding provider.

    Environment variable: ``OPENAI_API_KEY``
    Models: ``text-embedding-3-small``, ``text-embedding-3-large``
    """

    def __init__(
        self,
        api_key: str | None = None,
        model: str = "text-embedding-3-small",
        base_url: str = "https://api.openai.com/v1",
    ) -> None:
        self.api_key = api_key or os.environ.get("OPENAI_API_KEY", "")
        self.model = model
        self.base_url = base_url.rstrip("/")
        self._dimension = 1536 if "small" in model else 3072

    def embed(self, texts: list[str]) -> list[list[float]]:
        import httpx

        embeddings: list[list[float]] = []
        # OpenAI allows up to 2048 texts per batch
        batch_size = 100
        for i in range(0, len(texts), batch_size):
            batch = texts[i : i + batch_size]
            body = {
                "model": self.model,
                "input": batch,
            }
            try:
                with httpx.Client(timeout=60.0) as client:
                    response = client.post(
                        f"{self.base_url}/embeddings",
                        headers={
                            "Authorization": f"Bearer {self.api_key}",
                            "Content-Type": "application/json",
                        },
                        json=body,
                    )
                    response.raise_for_status()
                    data = response.json()
                    for item in data.get("data", []):
                        embeddings.append(item["embedding"])
            except httpx.HTTPStatusError as exc:
                logger.error("Embedding API error: %s", exc)
                # Fall back to zero vectors
                for _ in batch:
                    embeddings.append([0.0] * self._dimension)
        return embeddings

    def dimension(self) -> int:
        return self._dimension

    @property
    def name(self) -> str:
        return f"OpenAI-{self.model}"


# =========================================================================
# VectorStore
# =========================================================================


class VectorStore(ABC):
    """Abstract base for vector storage backends."""

    @abstractmethod
    def add(self, chunks: list[DocumentChunk]) -> None:
        """Store document chunks with their embeddings."""

    @abstractmethod
    def search(
        self, query_embedding: list[float], top_k: int = 5
    ) -> list[RetrievedChunk]:
        """Search for the top-k most similar chunks."""

    @abstractmethod
    def delete_by_source(self, source: str) -> None:
        """Delete all chunks from a given source."""

    @abstractmethod
    def clear(self) -> None:
        """Clear all stored vectors."""

    @abstractmethod
    def __len__(self) -> int:
        """Return the number of stored chunks."""


class InMemoryVectorStore(VectorStore):
    """In-memory vector store using cosine similarity.

    Suitable for small-to-medium document collections (up to ~10k
    chunks).  Thread-safe for single-process use.
    """

    def __init__(self, embedding_dim: int = 384) -> None:
        self._chunks: list[DocumentChunk] = []
        self._embedding_dim = embedding_dim

    def add(self, chunks: list[DocumentChunk]) -> None:
        for chunk in chunks:
            if chunk.embedding and len(chunk.embedding) != self._embedding_dim:
                raise ValueError(
                    f"Embedding dimension mismatch: expected {self._embedding_dim}, "
                    f"got {len(chunk.embedding)}"
                )
        self._chunks.extend(chunks)
        logger.info("Added %d chunks (total: %d)", len(chunks), len(self._chunks))

    def search(
        self, query_embedding: list[float], top_k: int = 5
    ) -> list[RetrievedChunk]:
        if not self._chunks:
            return []

        query_vec = np.array(query_embedding, dtype=np.float32)
        query_norm = np.linalg.norm(query_vec)
        if query_norm > 0:
            query_vec /= query_norm

        # Compute cosine similarities
        similarities: list[tuple[float, int]] = []
        for i, chunk in enumerate(self._chunks):
            if not chunk.embedding:
                similarities.append((0.0, i))
                continue
            chunk_vec = np.array(chunk.embedding, dtype=np.float32)
            chunk_norm = np.linalg.norm(chunk_vec)
            if chunk_norm > 0:
                chunk_vec /= chunk_norm
            sim = float(np.dot(query_vec, chunk_vec))
            similarities.append((sim, i))

        # Sort by similarity (descending)
        similarities.sort(key=lambda x: x[0], reverse=True)

        results: list[RetrievedChunk] = []
        for rank, (sim, idx) in enumerate(similarities[:top_k], 1):
            results.append(
                RetrievedChunk(
                    chunk=self._chunks[idx],
                    similarity=round(sim, 6),
                    rank=rank,
                )
            )
        return results

    def delete_by_source(self, source: str) -> None:
        original_count = len(self._chunks)
        self._chunks = [c for c in self._chunks if c.source != source]
        removed = original_count - len(self._chunks)
        logger.info("Deleted %d chunks from source: %s", removed, source)

    def clear(self) -> None:
        self._chunks.clear()
        logger.info("Vector store cleared")

    def __len__(self) -> int:
        return len(self._chunks)


class PgVectorStore(VectorStore):
    """PostgreSQL + pgvector storage backend.

    Requires:
    - PostgreSQL 14+ with pgvector extension
    - ``psycopg2-binary`` Python package

    Connection via environment variable ``DATABASE_URL`` or constructor
    parameters.
    """

    def __init__(
        self,
        connection_string: str | None = None,
        table_name: str = "powsy365_chunks",
        embedding_dim: int = 384,
    ) -> None:
        self.connection_string = (
            connection_string
            or os.environ.get("DATABASE_URL")
            or "postgresql://localhost:5432/powsy365"
        )
        self.table_name = table_name
        self._embedding_dim = embedding_dim
        self._conn = None
        self._ensure_connection()
        self._init_schema()

    def _ensure_connection(self) -> None:
        try:
            import psycopg2  # type: ignore[import-untyped]
            from psycopg2.extras import execute_values  # type: ignore[import-untyped]

            self._conn = psycopg2.connect(self.connection_string)
            logger.info("Connected to PostgreSQL")
        except ImportError:
            raise ImportError(
                "PgVectorStore requires psycopg2-binary. "
                "Install: pip install psycopg2-binary"
            )
        except Exception as exc:
            raise ConnectionError(f"Failed to connect to PostgreSQL: {exc}")

    def _init_schema(self) -> None:
        """Create pgvector extension and table if not exists."""
        with self._conn.cursor() as cur:  # type: ignore[union-attr]
            cur.execute("CREATE EXTENSION IF NOT EXISTS vector")
            cur.execute(f"""
                CREATE TABLE IF NOT EXISTS {self.table_name} (
                    chunk_id TEXT PRIMARY KEY,
                    doc_id TEXT NOT NULL,
                    source TEXT NOT NULL,
                    content TEXT NOT NULL,
                    title TEXT,
                    chunk_index INTEGER DEFAULT 0,
                    total_chunks INTEGER DEFAULT 1,
                    embedding vector({self._embedding_dim}),
                    metadata JSONB DEFAULT '{{}}',
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
            """)
            cur.execute(f"""
                CREATE INDEX IF NOT EXISTS idx_{self.table_name}_source
                ON {self.table_name} (source)
            """)
            # IVFFlat index for approximate nearest neighbor search
            cur.execute(f"""
                CREATE INDEX IF NOT EXISTS idx_{self.table_name}_embedding
                ON {self.table_name}
                USING ivfflat (embedding vector_cosine_ops)
                WITH (lists = 100)
            """)
        self._conn.commit()  # type: ignore[union-attr]

    def add(self, chunks: list[DocumentChunk]) -> None:
        if not chunks:
            return
        rows = [
            (
                c.chunk_id,
                c.doc_id,
                c.source,
                c.content,
                c.title,
                c.chunk_index,
                c.total_chunks,
                c.embedding,
                json.dumps(c.metadata),
            )
            for c in chunks
        ]
        with self._conn.cursor() as cur:  # type: ignore[union-attr]
            from psycopg2.extras import execute_values  # type: ignore[import-untyped]

            execute_values(
                cur,
                f"""
                INSERT INTO {self.table_name}
                (chunk_id, doc_id, source, content, title, chunk_index,
                 total_chunks, embedding, metadata)
                VALUES %s
                ON CONFLICT (chunk_id) DO UPDATE SET
                    content = EXCLUDED.content,
                    embedding = EXCLUDED.embedding,
                    metadata = EXCLUDED.metadata
                """,
                rows,
                template="(%s, %s, %s, %s, %s, %s, %s, %s::vector, %s::jsonb)",
            )
        self._conn.commit()  # type: ignore[union-attr]
        logger.info("Stored %d chunks in pgvector", len(chunks))

    def search(
        self, query_embedding: list[float], top_k: int = 5
    ) -> list[RetrievedChunk]:
        with self._conn.cursor() as cur:  # type: ignore[union-attr]
            embedding_str = "[" + ",".join(str(v) for v in query_embedding) + "]"
            cur.execute(f"""
                SELECT chunk_id, doc_id, source, content, title,
                       chunk_index, total_chunks, metadata,
                       1 - (embedding <=> %s::vector) AS similarity
                FROM {self.table_name}
                ORDER BY embedding <=> %s::vector
                LIMIT %s
            """, (embedding_str, embedding_str, top_k))

            results: list[RetrievedChunk] = []
            for rank, row in enumerate(cur.fetchall(), 1):
                chunk = DocumentChunk(
                    chunk_id=row[0],
                    doc_id=row[1],
                    source=row[2],
                    content=row[3],
                    title=row[4] or "",
                    chunk_index=row[5],
                    total_chunks=row[6],
                    metadata=row[7] or {},
                )
                results.append(
                    RetrievedChunk(chunk=chunk, similarity=row[8], rank=rank)
                )
            return results

    def delete_by_source(self, source: str) -> None:
        with self._conn.cursor() as cur:  # type: ignore[union-attr]
            cur.execute(
                f"DELETE FROM {self.table_name} WHERE source = %s",
                (source,),
            )
        self._conn.commit()  # type: ignore[union-attr]

    def clear(self) -> None:
        with self._conn.cursor() as cur:  # type: ignore[union-attr]
            cur.execute(f"TRUNCATE TABLE {self.table_name}")
        self._conn.commit()  # type: ignore[union-attr]

    def __len__(self) -> int:
        with self._conn.cursor() as cur:  # type: ignore[union-attr]
            cur.execute(f"SELECT COUNT(*) FROM {self.table_name}")
            return cur.fetchone()[0]


# =========================================================================
# Retriever
# =========================================================================


class Retriever:
    """Semantic document retriever.

    Combines an embedding provider and vector store to perform
    relevance-based document search.
    """

    def __init__(
        self,
        embedding: EmbeddingProvider,
        store: VectorStore,
        top_k: int = 5,
    ) -> None:
        self.embedding = embedding
        self.store = store
        self.top_k = top_k

    def retrieve(self, query: str) -> list[RetrievedChunk]:
        """Retrieve the top-k most relevant chunks for *query*.

        Parameters
        ----------
        query: Natural language query string.

        Returns
        -------
        list[RetrievedChunk]
            Ranked list of retrieved document chunks.
        """
        t0 = time.perf_counter()
        query_embeddings = self.embedding.embed([query])
        if not query_embeddings:
            return []

        results = self.store.search(query_embeddings[0], self.top_k)
        elapsed = (time.perf_counter() - t0) * 1000.0
        logger.info(
            "Retrieved %d chunks for query in %.1f ms",
            len(results),
            elapsed,
        )
        return results

    def retrieve_context(self, query: str, max_chars: int = 4000) -> str:
        """Retrieve chunks and format as an augmented context string.

        Parameters
        ----------
        query: User query.
        max_chars: Maximum length of the returned context.

        Returns
        -------
        str
            Formatted context ready for LLM prompt injection.
        """
        results = self.retrieve(query)
        if not results:
            return ""

        lines: list[str] = ["=== Technical Documentation Context ===", ""]
        total_chars = sum(len(l) for l in lines)

        for result in results:
            header = f"--- [{result.rank}] {result.chunk.source}"
            if result.chunk.title:
                header += f" ({result.chunk.title})"
            header += f" [relevance: {result.similarity:.3f}] ---"

            block = f"{header}\n{result.chunk.content}\n"
            if total_chars + len(block) > max_chars:
                break
            lines.append(block)
            total_chars += len(block)

        return "\n".join(lines)


# =========================================================================
# RAGChain
# =========================================================================


class RAGChain:
    """End-to-end RAG pipeline: question -> context -> answer.

    Combines all RAG components into a single callable chain that
    can be integrated with any LLM provider.

    Example::

        from powsy365.ai.llm_providers import DeepSeekProvider
        from powsy365.ai.rag_pipeline import RAGChain

        rag = RAGChain(
            retriever=retriever,
            llm_provider=DeepSeekProvider(),
        )
        answer = rag.ask("What are the voltage limits for transformers?")
    """

    def __init__(
        self,
        retriever: Retriever,
        llm_provider: Any,  # LLMProvider instance
        system_prompt: str = (
            "You are an expert power systems engineer. Answer the user's "
            "question using ONLY the provided technical documentation context. "
            "If the context doesn't contain the answer, say so clearly. "
            "Be precise, cite specific values, and use technical terminology."
        ),
        max_context_chars: int = 4000,
        max_tokens: int = 2048,
        temperature: float = 0.3,
    ) -> None:
        self.retriever = retriever
        self.llm = llm_provider
        self.system_prompt = system_prompt
        self.max_context_chars = max_context_chars
        self.max_tokens = max_tokens
        self.temperature = temperature

    def ask(self, question: str) -> str:
        """Ask a question and get an AI-generated answer grounded in docs.

        Parameters
        ----------
        question: Natural language question.

        Returns
        -------
        str: Generated answer with citations from technical docs.
        """
        # 1. Retrieve relevant context
        context = self.retriever.retrieve_context(
            question, max_chars=self.max_context_chars
        )

        # 2. Build augmented prompt
        messages = [
            {"role": "system", "content": self.system_prompt},
            {
                "role": "user",
                "content": self._build_prompt(question, context),
            },
        ]

        # 3. Query LLM
        try:
            from powsy365.ai.llm_providers import LLMRequest, Message

            req = LLMRequest(
                messages=[
                    Message(role="system", content=self.system_prompt),
                    Message(
                        role="user",
                        content=self._build_prompt(question, context),
                    ),
                ],
                max_tokens=self.max_tokens,
                temperature=self.temperature,
            )
            resp = self.llm.chat(req)
            return resp.content if resp.success else f"Error: {resp.error_message}"
        except Exception as exc:
            logger.error("RAG query failed: %s", exc)
            return f"Failed to generate answer: {exc}"

    def _build_prompt(self, question: str, context: str) -> str:
        """Build the final prompt with context and question."""
        prompt_parts: list[str] = []
        if context:
            prompt_parts.append("Technical documentation context:")
            prompt_parts.append(context)
            prompt_parts.append("")
        prompt_parts.append(f"Question: {question}")
        prompt_parts.append("")
        prompt_parts.append(
            "Provide a detailed, technically accurate answer based on the "
            "documentation above. Include specific values, standards, and "
            "recommendations where applicable."
        )
        return "\n".join(prompt_parts)

    def ingest_document(self, path: str | Path) -> int:
        """Ingest a document into the RAG pipeline.

        Parameters
        ----------
        path: Path to the document file.

        Returns
        -------
        int: Number of chunks created and stored.
        """
        loader = DocumentLoader()
        splitter = TextSplitter()

        doc = loader.load(path)
        chunks = splitter.split_document(doc)

        # Compute embeddings
        texts = [c.content for c in chunks]
        embeddings = self.retriever.embedding.embed(texts)
        for chunk, emb in zip(chunks, embeddings):
            chunk.embedding = emb

        self.retriever.store.add(chunks)
        logger.info("Ingested %s: %d chunks", path, len(chunks))
        return len(chunks)

    def ingest_directory(self, directory: str | Path, pattern: str = "*") -> int:
        """Ingest all matching documents from a directory.

        Returns
        -------
        int: Total number of chunks created and stored.
        """
        loader = DocumentLoader()
        splitter = TextSplitter()

        docs = loader.load_directory(directory, pattern)
        total_chunks = 0

        for doc in docs:
            chunks = splitter.split_document(doc)
            texts = [c.content for c in chunks]
            embeddings = self.retriever.embedding.embed(texts)
            for chunk, emb in zip(chunks, embeddings):
                chunk.embedding = emb
            self.retriever.store.add(chunks)
            total_chunks += len(chunks)

        logger.info(
            "Ingested %d documents from %s: %d total chunks",
            len(docs),
            directory,
            total_chunks,
        )
        return total_chunks

    def __repr__(self) -> str:
        return (
            f"<RAGChain embedding={self.retriever.embedding.name} "
            f"store_size={len(self.retriever.store)} "
            f"llm={self.llm}>"
        )
