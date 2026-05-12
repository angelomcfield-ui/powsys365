#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
POWSYS365 - User Manager (Python)
Gestor de usuarios con PostgreSQL backend.

Funcionalidades:
- Registro de usuarios con bcrypt
- Autenticacion con verificacion de password
- Cambio de password
- Reset de password
- Validacion de email/password
- Gestion de perfiles
- Asignacion de licencias

Requiere:
    psycopg2-binary>=2.9.0
    bcrypt>=4.0.0
    email-validator>=2.0.0
"""

from __future__ import annotations

import logging
import re
import secrets
import string
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from enum import Enum
from typing import Any, Dict, List, Optional, Tuple

import bcrypt
import psycopg2
from psycopg2.extras import RealDictCursor

logger = logging.getLogger("powsys365.users")


class UserRole(Enum):
    """Roles de usuario."""
    USER = "user"
    ADMIN = "admin"
    SUPPORT = "support"


class UserStatus(Enum):
    """Estados de usuario."""
    ACTIVE = "active"
    INACTIVE = "inactive"
    SUSPENDED = "suspended"
    PENDING_VERIFICATION = "pending_verification"


@dataclass
class User:
    """Modelo de usuario."""
    id: str  # UUID
    email: str
    full_name: str = ""
    company: str = ""
    phone: str = ""
    license_key: str = ""
    role: str = "user"
    status: str = "active"
    is_verified: bool = False
    created_at: Optional[datetime] = None
    updated_at: Optional[datetime] = None
    last_login: Optional[datetime] = None

    def to_dict(self) -> Dict[str, Any]:
        """Serializa a dict (excluye datos sensibles)."""
        return {
            "id": self.id,
            "email": self.email,
            "full_name": self.full_name,
            "company": self.company,
            "phone": self.phone,
            "license_key": self.license_key,
            "role": self.role,
            "status": self.status,
            "is_verified": self.is_verified,
            "created_at": self.created_at.isoformat() if self.created_at else None,
            "last_login": self.last_login.isoformat() if self.last_login else None,
        }


@dataclass
class UserCredentials:
    """Credenciales para autenticacion."""
    email: str
    password: str


@dataclass
class AuthResult:
    """Resultado de autenticacion."""
    success: bool
    user: Optional[User] = None
    token: str = ""
    error_code: str = ""
    error_message: str = ""


class UserManagerError(Exception):
    """Error del gestor de usuarios."""
    pass


class ValidationError(Exception):
    """Error de validacion."""
    def __init__(self, field: str, message: str):
        self.field = field
        self.message = message
        super().__init__(f"{field}: {message}")


class UserManager:
    """
    Gestor de usuarios POWSYS365.
    
    Maneja el ciclo completo de vida del usuario:
    registro, autenticacion, gestion de password, perfiles.
    
    Usage:
        db_config = {
            "host": "localhost",
            "port": 5432,
            "database": "powsys365",
            "user": "postgres",
            "password": "secret"
        }
        
        um = UserManager(db_config)
        
        # Registrar usuario
        user = um.register_user("john@example.com", "SecurePass123!", "John Doe")
        
        # Autenticar
        result = um.authenticate("john@example.com", "SecurePass123!")
        if result.success:
            print(f"Welcome, {result.user.full_name}!")
    """

    # Configuracion de password
    MIN_PASSWORD_LENGTH = 8
    MAX_PASSWORD_LENGTH = 128
    BCRYPT_ROUNDS = 12
    RESET_TOKEN_EXPIRY_HOURS = 24
    VERIFICATION_TOKEN_EXPIRY_HOURS = 48

    def __init__(self, db_config: Dict[str, Any]):
        self.db_config = db_config
        self._db = None

    # ------------------------------------------------------------------
    # Database Connection
    # ------------------------------------------------------------------
    def _connect(self) -> psycopg2.extensions.connection:
        """Establece conexion a PostgreSQL."""
        if self._db is None or self._db.closed:
            self._db = psycopg2.connect(**self.db_config)
        return self._db

    def _cursor(self, dict_cursor: bool = True):
        """Obtiene un cursor."""
        conn = self._connect()
        if dict_cursor:
            return conn.cursor(cursor_factory=RealDictCursor)
        return conn.cursor()

    def _commit(self) -> None:
        """Commit de la transaccion actual."""
        if self._db:
            self._db.commit()

    def _rollback(self) -> None:
        """Rollback de la transaccion actual."""
        if self._db:
            self._db.rollback()

    def close(self) -> None:
        """Cierra la conexion a la base de datos."""
        if self._db and not self._db.closed:
            self._db.close()
            self._db = None

    def __enter__(self) -> "UserManager":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    # ------------------------------------------------------------------
    # Password Hashing (bcrypt)
    # ------------------------------------------------------------------
    @staticmethod
    def _hash_password(password: str) -> str:
        """Genera hash bcrypt de un password."""
        password_bytes = password.encode("utf-8")
        salt = bcrypt.gensalt(rounds=UserManager.BCRYPT_ROUNDS)
        hashed = bcrypt.hashpw(password_bytes, salt)
        return hashed.decode("utf-8")

    @staticmethod
    def _verify_password(password: str, password_hash: str) -> bool:
        """Verifica un password contra su hash bcrypt."""
        try:
            password_bytes = password.encode("utf-8")
            hash_bytes = password_hash.encode("utf-8")
            return bcrypt.checkpw(password_bytes, hash_bytes)
        except Exception:
            return False

    # ------------------------------------------------------------------
    # Token Generation
    # ------------------------------------------------------------------
    @staticmethod
    def _generate_token(length: int = 32) -> str:
        """Genera un token criptograficamente seguro."""
        alphabet = string.ascii_letters + string.digits
        return "".join(secrets.choice(alphabet) for _ in range(length))

    @staticmethod
    def _generate_uuid() -> str:
        """Genera un UUID v4."""
        import uuid
        return str(uuid.uuid4())

    # ------------------------------------------------------------------
    # Validation
    # ------------------------------------------------------------------
    @staticmethod
    def validate_email(email: str) -> bool:
        """
        Valida formato de email.
        
        Returns:
            True si el email es valido
        
        Raises:
            ValidationError: Si el formato es invalido
        """
        if not email:
            raise ValidationError("email", "Email is required")

        pattern = r"^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$"
        if not re.match(pattern, email):
            raise ValidationError("email", "Invalid email format")

        if len(email) > 255:
            raise ValidationError("email", "Email too long (max 255 characters)")

        return True

    @staticmethod
    def validate_password(password: str) -> bool:
        """
        Valida fortaleza de password.
        
        Requisitos:
        - Minimo 8 caracteres
        - Maximo 128 caracteres
        - Al menos 1 mayuscula
        - Al menos 1 minuscula
        - Al menos 1 digito
        - Al menos 1 caracter especial
        
        Raises:
            ValidationError: Si no cumple requisitos
        """
        if not password:
            raise ValidationError("password", "Password is required")

        if len(password) < UserManager.MIN_PASSWORD_LENGTH:
            raise ValidationError(
                "password",
                f"Password must be at least {UserManager.MIN_PASSWORD_LENGTH} characters"
            )

        if len(password) > UserManager.MAX_PASSWORD_LENGTH:
            raise ValidationError(
                "password",
                f"Password must be at most {UserManager.MAX_PASSWORD_LENGTH} characters"
            )

        checks = [
            (lambda p: any(c.isupper() for c in p), "at least one uppercase letter"),
            (lambda p: any(c.islower() for c in p), "at least one lowercase letter"),
            (lambda p: any(c.isdigit() for c in p), "at least one digit"),
            (lambda p: any(c in string.punctuation for c in p), "at least one special character"),
        ]

        for check, description in checks:
            if not check(password):
                raise ValidationError("password", f"Password must contain {description}")

        return True

    @staticmethod
    def validate_name(name: str, field: str = "name") -> bool:
        """Valida un nombre."""
        if not name:
            return True  # Optional
        if len(name) > 255:
            raise ValidationError(field, "Name too long (max 255 characters)")
        return True

    # ------------------------------------------------------------------
    # User Registration
    # ------------------------------------------------------------------
    def register_user(
        self,
        email: str,
        password: str,
        full_name: str = "",
        company: str = "",
        phone: str = "",
        license_key: str = "",
    ) -> User:
        """
        Registra un nuevo usuario.
        
        Args:
            email: Correo electronico (unico)
            password: Contrasena (validada)
            full_name: Nombre completo
            company: Empresa
            phone: Telefono
            license_key: Licencia asociada (opcional)
        
        Returns:
            Objeto User creado
        
        Raises:
            ValidationError: Si los datos son invalidos
            UserManagerError: Si el email ya existe
        """
        # Validate inputs
        self.validate_email(email)
        self.validate_password(password)
        self.validate_name(full_name, "full_name")

        # Check if email exists
        existing = self.find_by_email(email)
        if existing:
            raise UserManagerError(f"Email already registered: {email}")

        # Hash password
        password_hash = self._hash_password(password)

        # Generate tokens
        user_id = self._generate_uuid()
        verification_token = self._generate_token(32)
        verification_expires = datetime.now(timezone.utc) + timedelta(
            hours=self.VERIFICATION_TOKEN_EXPIRY_HOURS
        )

        # Insert user
        cur = self._cursor()
        try:
            cur.execute(
                """
                INSERT INTO users (
                    id, email, password_hash, full_name, company, phone,
                    license_key, is_verified, verification_token,
                    verification_token_expires, status, role,
                    created_at, updated_at
                ) VALUES (
                    %s, %s, %s, %s, %s, %s,
                    %s, FALSE, %s,
                    %s, 'pending_verification', 'user',
                    NOW(), NOW()
                )
                RETURNING *
                """,
                (
                    user_id, email, password_hash, full_name, company, phone,
                    license_key, verification_token, verification_expires
                )
            )
            row = cur.fetchone()
            self._commit()

            logger.info("User registered: %s (%s)", user_id, email)
            return self._row_to_user(row)

        except psycopg2.IntegrityError as e:
            self._rollback()
            raise UserManagerError(f"Email already registered: {email}") from e
        except Exception as e:
            self._rollback()
            raise UserManagerError(f"Registration failed: {e}") from e
        finally:
            cur.close()

    # ------------------------------------------------------------------
    # Authentication
    # ------------------------------------------------------------------
    def authenticate(
        self,
        email: str,
        password: str
    ) -> AuthResult:
        """
        Autentica un usuario.
        
        Args:
            email: Correo electronico
            password: Contrasena
        
        Returns:
            AuthResult con el resultado
        """
        if not email or not password:
            return AuthResult(
                success=False,
                error_code="MISSING_CREDENTIALS",
                error_message="Email and password are required"
            )

        try:
            cur = self._cursor()
            cur.execute(
                "SELECT * FROM users WHERE email = %s AND status IN ('active', 'pending_verification')",
                (email,)
            )
            row = cur.fetchone()
            cur.close()

            if not row:
                # Constant-time to prevent timing attacks
                self._verify_password(password, "")
                return AuthResult(
                    success=False,
                    error_code="INVALID_CREDENTIALS",
                    error_message="Invalid email or password"
                )

            # Verify password
            stored_hash = row["password_hash"]
            if not self._verify_password(password, stored_hash):
                return AuthResult(
                    success=False,
                    error_code="INVALID_CREDENTIALS",
                    error_message="Invalid email or password"
                )

            # Check status
            if row["status"] == "suspended":
                return AuthResult(
                    success=False,
                    error_code="ACCOUNT_SUSPENDED",
                    error_message="Account has been suspended"
                )

            # Update last login
            self._update_last_login(row["id"])

            # Generate session token
            token = self._generate_token(64)

            user = self._row_to_user(row)
            logger.info("User authenticated: %s (%s)", user.id, email)

            return AuthResult(success=True, user=user, token=token)

        except Exception as e:
            logger.error("Authentication error: %s", e)
            return AuthResult(
                success=False,
                error_code="AUTH_ERROR",
                error_message="Authentication failed"
            )

    # ------------------------------------------------------------------
    # Password Management
    # ------------------------------------------------------------------
    def change_password(
        self,
        user_id: str,
        current_password: str,
        new_password: str
    ) -> bool:
        """
        Cambia la contrasena de un usuario.
        
        Args:
            user_id: ID del usuario
            current_password: Contrasena actual
            new_password: Nueva contrasena
        
        Returns:
            True si el cambio fue exitoso
        """
        # Validate new password
        self.validate_password(new_password)

        cur = self._cursor()
        try:
            # Get current password hash
            cur.execute(
                "SELECT password_hash FROM users WHERE id = %s",
                (user_id,)
            )
            row = cur.fetchone()

            if not row:
                return False

            # Verify current password
            if not self._verify_password(current_password, row["password_hash"]):
                return False

            # Update password
            new_hash = self._hash_password(new_password)
            cur.execute(
                "UPDATE users SET password_hash = %s, updated_at = NOW() WHERE id = %s",
                (new_hash, user_id)
            )
            self._commit()
            logger.info("Password changed for user: %s", user_id)
            return True

        except Exception as e:
            self._rollback()
            logger.error("Password change failed: %s", e)
            return False
        finally:
            cur.close()

    def generate_password_reset_token(self, email: str) -> Optional[str]:
        """
        Genera un token para reset de password.
        
        Args:
            email: Email del usuario
        
        Returns:
            Token de reset o None si el email no existe
        """
        token = self._generate_token(32)
        expires = datetime.now(timezone.utc) + timedelta(
            hours=self.RESET_TOKEN_EXPIRY_HOURS
        )

        cur = self._cursor()
        try:
            cur.execute(
                """
                UPDATE users
                SET reset_token = %s, reset_token_expires = %s, updated_at = NOW()
                WHERE email = %s
                RETURNING id
                """,
                (token, expires, email)
            )
            row = cur.fetchone()
            self._commit()

            if row:
                logger.info("Password reset token generated for: %s", email)
                return token
            return None

        except Exception as e:
            self._rollback()
            logger.error("Reset token generation failed: %s", e)
            return None
        finally:
            cur.close()

    def reset_password(self, token: str, new_password: str) -> bool:
        """
        Resetea la password usando un token.
        
        Args:
            token: Token de reset
            new_password: Nueva contrasena
        
        Returns:
            True si el reset fue exitoso
        """
        self.validate_password(new_password)

        cur = self._cursor()
        try:
            cur.execute(
                """
                SELECT id, reset_token_expires
                FROM users
                WHERE reset_token = %s
                AND status = 'active'
                """,
                (token,)
            )
            row = cur.fetchone()

            if not row:
                return False

            # Check expiry
            expires = row["reset_token_expires"]
            if expires and expires < datetime.now(timezone.utc):
                return False

            # Update password
            new_hash = self._hash_password(new_password)
            cur.execute(
                """
                UPDATE users
                SET password_hash = %s, reset_token = NULL,
                    reset_token_expires = NULL, updated_at = NOW()
                WHERE id = %s
                """,
                (new_hash, row["id"])
            )
            self._commit()
            logger.info("Password reset successful for user: %s", row["id"])
            return True

        except Exception as e:
            self._rollback()
            logger.error("Password reset failed: %s", e)
            return False
        finally:
            cur.close()

    # ------------------------------------------------------------------
    # Email Verification
    # ------------------------------------------------------------------
    def verify_email(self, token: str) -> bool:
        """
        Verifica el email de un usuario.
        
        Args:
            token: Token de verificacion
        
        Returns:
            True si la verificacion fue exitosa
        """
        cur = self._cursor()
        try:
            cur.execute(
                """
                UPDATE users
                SET is_verified = TRUE,
                    status = CASE WHEN status = 'pending_verification' THEN 'active' ELSE status END,
                    verification_token = NULL,
                    verification_token_expires = NULL,
                    updated_at = NOW()
                WHERE verification_token = %s
                AND (verification_token_expires IS NULL OR verification_token_expires > NOW())
                RETURNING id
                """,
                (token,)
            )
            row = cur.fetchone()
            self._commit()

            if row:
                logger.info("Email verified for user: %s", row["id"])
                return True
            return False

        except Exception as e:
            self._rollback()
            logger.error("Email verification failed: %s", e)
            return False
        finally:
            cur.close()

    # ------------------------------------------------------------------
    # User Queries
    # ------------------------------------------------------------------
    def find_by_email(self, email: str) -> Optional[User]:
        """Busca usuario por email."""
        cur = self._cursor()
        try:
            cur.execute(
                "SELECT * FROM users WHERE email = %s",
                (email,)
            )
            row = cur.fetchone()
            return self._row_to_user(row) if row else None
        finally:
            cur.close()

    def find_by_id(self, user_id: str) -> Optional[User]:
        """Busca usuario por ID."""
        cur = self._cursor()
        try:
            cur.execute("SELECT * FROM users WHERE id = %s", (user_id,))
            row = cur.fetchone()
            return self._row_to_user(row) if row else None
        finally:
            cur.close()

    def list_users(
        self,
        status: str = "",
        limit: int = 100,
        offset: int = 0
    ) -> List[User]:
        """Lista usuarios con filtros opcionales."""
        cur = self._cursor()
        try:
            query = "SELECT * FROM users WHERE 1=1"
            params = []

            if status:
                query += " AND status = %s"
                params.append(status)

            query += " ORDER BY created_at DESC LIMIT %s OFFSET %s"
            params.extend([limit, offset])

            cur.execute(query, params)
            rows = cur.fetchall()
            return [self._row_to_user(row) for row in rows]
        finally:
            cur.close()

    def count_users(self, status: str = "") -> int:
        """Cuenta usuarios."""
        cur = self._cursor(dict_cursor=False)
        try:
            query = "SELECT COUNT(*) FROM users"
            params = []
            if status:
                query += " WHERE status = %s"
                params.append(status)
            cur.execute(query, params)
            return cur.fetchone()[0]
        finally:
            cur.close()

    # ------------------------------------------------------------------
    # User Updates
    # ------------------------------------------------------------------
    def update_user(
        self,
        user_id: str,
        full_name: str = "",
        company: str = "",
        phone: str = "",
    ) -> Optional[User]:
        """
        Actualiza datos del perfil de usuario.
        
        Solo actualiza campos que se proporcionan (no vacios).
        """
        updates = []
        params = []

        if full_name:
            updates.append("full_name = %s")
            params.append(full_name)
        if company:
            updates.append("company = %s")
            params.append(company)
        if phone:
            updates.append("phone = %s")
            params.append(phone)

        if not updates:
            return self.find_by_id(user_id)

        updates.append("updated_at = NOW()")
        params.append(user_id)

        cur = self._cursor()
        try:
            cur.execute(
                f"UPDATE users SET {', '.join(updates)} WHERE id = %s RETURNING *",
                params
            )
            row = cur.fetchone()
            self._commit()
            return self._row_to_user(row) if row else None
        except Exception as e:
            self._rollback()
            logger.error("User update failed: %s", e)
            return None
        finally:
            cur.close()

    def assign_license(self, user_id: str, license_key: str) -> bool:
        """Asigna una licencia a un usuario."""
        cur = self._cursor()
        try:
            cur.execute(
                "UPDATE users SET license_key = %s, updated_at = NOW() WHERE id = %s",
                (license_key, user_id)
            )
            self._commit()
            logger.info("License %s assigned to user %s", license_key, user_id)
            return True
        except Exception as e:
            self._rollback()
            logger.error("License assignment failed: %s", e)
            return False
        finally:
            cur.close()

    def deactivate_user(self, user_id: str) -> bool:
        """Desactiva un usuario."""
        cur = self._cursor()
        try:
            cur.execute(
                "UPDATE users SET status = 'inactive', updated_at = NOW() WHERE id = %s",
                (user_id,)
            )
            self._commit()
            logger.info("User deactivated: %s", user_id)
            return True
        except Exception as e:
            self._rollback()
            return False
        finally:
            cur.close()

    def reactivate_user(self, user_id: str) -> bool:
        """Reactiva un usuario."""
        cur = self._cursor()
        try:
            cur.execute(
                "UPDATE users SET status = 'active', updated_at = NOW() WHERE id = %s",
                (user_id,)
            )
            self._commit()
            logger.info("User reactivated: %s", user_id)
            return True
        except Exception as e:
            self._rollback()
            return False
        finally:
            cur.close()

    # ------------------------------------------------------------------
    # Internal Helpers
    # ------------------------------------------------------------------
    def _row_to_user(self, row: Optional[Dict]) -> Optional[User]:
        """Convierte una fila de DB a User."""
        if not row:
            return None
        return User(
            id=str(row["id"]),
            email=row["email"],
            full_name=row.get("full_name", ""),
            company=row.get("company", ""),
            phone=row.get("phone", ""),
            license_key=row.get("license_key", ""),
            role=row.get("role", "user"),
            status=row.get("status", "active"),
            is_verified=row.get("is_verified", False),
            created_at=row.get("created_at"),
            updated_at=row.get("updated_at"),
            last_login=row.get("last_login"),
        )

    def _update_last_login(self, user_id: str) -> None:
        """Actualiza el ultimo login."""
        cur = self._cursor()
        try:
            cur.execute(
                "UPDATE users SET last_login = NOW() WHERE id = %s",
                (user_id,)
            )
            self._commit()
        finally:
            cur.close()

    # ------------------------------------------------------------------
    # Health Check
    # ------------------------------------------------------------------
    def health_check(self) -> Dict[str, Any]:
        """Verifica la conexion a la base de datos."""
        try:
            cur = self._cursor(dict_cursor=False)
            cur.execute("SELECT 1")
            cur.fetchone()
            cur.close()
            return {
                "status": "healthy",
                "database": "connected",
                "timestamp": datetime.now(timezone.utc).isoformat()
            }
        except Exception as e:
            return {
                "status": "unhealthy",
                "database": str(e),
                "timestamp": datetime.now(timezone.utc).isoformat()
            }


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    
    # Example usage (requires PostgreSQL running)
    db_config = {
        "host": "localhost",
        "port": 5432,
        "database": "powsys365",
        "user": "postgres",
        "password": "postgres"
    }
    
    with UserManager(db_config) as um:
        print("Health:", um.health_check())
