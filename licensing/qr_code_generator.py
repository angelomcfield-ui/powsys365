#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
POWSYS365 - QR Code Generator for Licenses
Generador de codigos QR para licencias en formato SVG y PNG.

Funcionalidades:
- Generar QR con datos de licencia
- Exportar a SVG (escalable)
- Exportar a PNG con PIL
- Estilizado con logo/branding

Requiere:
    qrcode>=7.4.0
    Pillow>=9.0.0
"""

from __future__ import annotations

import base64
import io
import logging
from dataclasses import dataclass
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Dict, Optional, Tuple, Union

import qrcode
from qrcode.image.svg import SvgFillImage, SvgImage
from PIL import Image, ImageDraw, ImageFont

logger = logging.getLogger("powsys365.qr")


class QRStyle(Enum):
    """Estilos de QR disponibles."""
    MINIMAL = "minimal"           # Solo QR, sin texto
    DETAILED = "detailed"         # QR + info de licencia debajo
    BRANDED = "branded"           # QR con logo/branding XNOX
    COMPACT = "compact"           # QR pequeno con texto lateral


@dataclass
class LicenseQRData:
    """Datos para codificar en el QR."""
    license_key: str
    tier: str = "trial"
    expires_at: str = ""
    customer_email: str = ""
    company: str = ""
    activation_url: str = ""
    product: str = "POWSYS365"

    def to_json(self) -> str:
        """Serializa a JSON compacto."""
        import json
        return json.dumps({
            "v": 1,  # version
            "k": self.license_key,
            "t": self.tier,
            "e": self.expires_at,
            "p": self.product,
        }, separators=(",", ":"))

    def to_vcard_style(self) -> str:
        """Formato vCard-style para maxima compatibilidad."""
        lines = [
            "BEGIN:VPOWSYS",
            f"LICENSE:{self.license_key}",
            f"TIER:{self.tier}",
            f"PRODUCT:{self.product}",
        ]
        if self.expires_at:
            lines.append(f"EXPIRES:{self.expires_at}")
        if self.customer_email:
            lines.append(f"EMAIL:{self.customer_email}")
        lines.append("END:VPOWSYS")
        return "\n".join(lines)


class LicenseQRGenerator:
    """
    Generador de QR para licencias POWSYS365.
    
    Usage:
        gen = LicenseQRGenerator()
        
        data = LicenseQRData(
            license_key="PR12-ABCD-EFGH-IJKL",
            tier="pro",
            expires_at="2025-12-31"
        )
        
        # SVG
        svg_content = gen.generate_license_qr(data, style=QRStyle.DETAILED)
        with open("license_qr.svg", "w") as f:
            f.write(svg_content)
        
        # PNG
        png_bytes = gen.generate_png(data, size=400)
        with open("license_qr.png", "wb") as f:
            f.write(png_bytes)
    """

    # Brand colors
    BRAND_PRIMARY = "#007AFF"
    BRAND_SECONDARY = "#5856D6"
    BRAND_DARK = "#1C1C1E"
    BRAND_LIGHT = "#F2F2F7"
    BRAND_WHITE = "#FFFFFF"

    # Tier colors
    TIER_COLORS = {
        "trial": "#8E8E93",       # Gray
        "basic": "#34C759",       # Green
        "pro": "#007AFF",         # Blue
        "enterprise": "#AF52DE",  # Purple
        "lifetime": "#FF9500",    # Orange
    }

    def __init__(self):
        self._default_error_correction = qrcode.constants.ERROR_CORRECT_H

    def _get_tier_color(self, tier: str) -> str:
        """Obtiene el color asociado a un tier."""
        return self.TIER_COLORS.get(tier.lower(), self.BRAND_PRIMARY)

    def _create_qr_code(
        self,
        data: str,
        version: Optional[int] = None,
        box_size: int = 10,
        border: int = 4,
        error_correction: Optional[int] = None
    ) -> qrcode.QRCode:
        """Crea un objeto QRCode configurado."""
        ec = error_correction or self._default_error_correction
        qr = qrcode.QRCode(
            version=version,
            error_correction=ec,
            box_size=box_size,
            border=border,
        )
        qr.add_data(data)
        qr.make(fit=True)
        return qr

    # ------------------------------------------------------------------
    # SVG Generation
    # ------------------------------------------------------------------
    def generate_license_qr(
        self,
        license_data: LicenseQRData,
        style: QRStyle = QRStyle.DETAILED,
        size: int = 400,
        **kwargs
    ) -> str:
        """
        Genera un QR de licencia en formato SVG.
        
        Args:
            license_data: Datos de la licencia
            style: Estilo visual
            size: Tamano en pixeles
        
        Returns:
            String SVG
        """
        if style == QRStyle.MINIMAL:
            return self._generate_svg_minimal(license_data, size)
        elif style == QRStyle.DETAILED:
            return self._generate_svg_detailed(license_data, size)
        elif style == QRStyle.BRANDED:
            return self._generate_svg_branded(license_data, size)
        elif style == QRStyle.COMPACT:
            return self._generate_svg_compact(license_data, size)
        else:
            return self._generate_svg_detailed(license_data, size)

    def _generate_svg_minimal(
        self,
        data: LicenseQRData,
        size: int
    ) -> str:
        """QR minimalista - solo el codigo."""
        qr = self._create_qr_code(data.to_json(), box_size=10)
        
        # Generate SVG
        factory = SvgFillImage
        img = qr.make_image(fill_color="black", back_color="white", image_factory=factory)
        
        svg_bytes = io.BytesIO()
        img.save(svg_bytes)
        return svg_bytes.getvalue().decode("utf-8")

    def _generate_svg_detailed(
        self,
        data: LicenseQRData,
        size: int
    ) -> str:
        """QR con informacion de licencia debajo."""
        tier_color = self._get_tier_color(data.tier)
        
        qr = self._create_qr_code(data.to_json(), box_size=10)
        factory = SvgFillImage
        img = qr.make_image(fill_color=tier_color, back_color="white", image_factory=factory)
        
        svg_bytes = io.BytesIO()
        img.save(svg_bytes)
        qr_svg = svg_bytes.getvalue().decode("utf-8")

        # Extract the QR paths and wrap in styled SVG
        # Parse the generated SVG to extract path data
        import re
        
        # Extract viewBox and path elements
        viewbox_match = re.search(r'viewBox="([^"]+)"', qr_svg)
        viewbox = viewbox_match.group(1) if viewbox_match else "0 0 370 370"
        
        paths_match = re.findall(r'(<path[^>]+/>)', qr_svg)
        paths = "\n    ".join(paths_match)

        # Calculate dimensions
        vb_parts = viewbox.split()
        qr_width = int(vb_parts[2]) if len(vb_parts) > 2 else 370
        qr_height = int(vb_parts[3]) if len(vb_parts) > 3 else 370
        
        info_height = 100
        total_height = qr_height + info_height

        svg_template = f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {qr_width} {total_height}" width="{size}" height="{int(size * total_height / qr_width)}">
  <defs>
    <style>
      .qr-fill {{ fill: {tier_color}; }}
      .bg {{ fill: #FFFFFF; }}
      .text-primary {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 14px; fill: {self.BRAND_DARK}; font-weight: 600; }}
      .text-secondary {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 11px; fill: #8E8E93; }}
      .text-tiny {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 9px; fill: #C7C7CC; }}
      .badge {{ fill: {tier_color}; rx: 4; }}
      .badge-text {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 10px; fill: #FFFFFF; font-weight: 700; }}
    </style>
  </defs>
  
  <!-- Background -->
  <rect width="{qr_width}" height="{total_height}" rx="12" class="bg"/>
  
  <!-- QR Code -->
  <g transform="translate(0, 0)">
    {paths}
  </g>
  
  <!-- Divider -->
  <line x1="20" y1="{qr_height}" x2="{qr_width - 20}" y2="{qr_height}" stroke="#E5E5EA" stroke-width="1"/>
  
  <!-- License Info -->
  <g transform="translate(20, {qr_height + 15})">
    <!-- Tier Badge -->
    <rect x="0" y="0" width="{min(80, len(data.tier) * 12 + 16)}" height="20" class="badge"/>
    <text x="{min(80, len(data.tier) * 12 + 16) / 2}" y="14" text-anchor="middle" class="badge-text">{data.tier.upper()}</text>
    
    <!-- License Key -->
    <text x="0" y="42" class="text-primary">{data.license_key}</text>
    
    <!-- Expiration -->
    <text x="0" y="60" class="text-secondary">{f"Expires: {data.expires_at}" if data.expires_at else "Lifetime License"}</text>
    
    <!-- Product -->
    <text x="0" y="78" class="text-tiny">{data.product} by XNOX L.L.C</text>
  </g>
</svg>"""
        return svg_template

    def _generate_svg_branded(
        self,
        data: LicenseQRData,
        size: int
    ) -> str:
        """QR con branding XNOX."""
        tier_color = self._get_tier_color(data.tier)
        
        # Generate base QR
        qr = self._create_qr_code(data.to_json(), box_size=10)
        factory = SvgFillImage
        img = qr.make_image(fill_color=tier_color, back_color="white", image_factory=factory)
        
        svg_bytes = io.BytesIO()
        img.save(svg_bytes)
        qr_svg = svg_bytes.getvalue().decode("utf-8")

        import re
        viewbox_match = re.search(r'viewBox="([^"]+)"', qr_svg)
        viewbox = viewbox_match.group(1) if viewbox_match else "0 0 370 370"
        paths_match = re.findall(r'(<path[^>]+/>)', qr_svg)
        paths = "\n    ".join(paths_match)

        vb_parts = viewbox.split()
        qr_width = int(vb_parts[2]) if len(vb_parts) > 2 else 370
        
        # Add header with branding
        header_height = 40
        footer_height = 80
        total_height = qr_width + header_height + footer_height

        svg_template = f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {qr_width} {total_height}" width="{size}" height="{int(size * total_height / qr_width)}">
  <defs>
    <style>
      .qr-fill {{ fill: {tier_color}; }}
      .header-bg {{ fill: {tier_color}; }}
      .bg {{ fill: #FFFFFF; }}
      .header-text {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 16px; fill: #FFFFFF; font-weight: 700; }}
      .text-primary {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 14px; fill: {self.BRAND_DARK}; font-weight: 600; }}
      .text-secondary {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 11px; fill: #8E8E93; }}
    </style>
  </defs>
  
  <!-- Background -->
  <rect width="{qr_width}" height="{total_height}" rx="12" class="bg"/>
  
  <!-- Header -->
  <rect x="0" y="0" width="{qr_width}" height="{header_height}" rx="12" class="header-bg"/>
  <rect x="0" y="20" width="{qr_width}" height="{header_height - 20}" class="header-bg"/>
  <text x="{qr_width / 2}" y="26" text-anchor="middle" class="header-text">XNOX L.L.C - POWSYS365</text>
  
  <!-- QR Code -->
  <g transform="translate(0, {header_height})">
    {paths}
  </g>
  
  <!-- Footer -->
  <g transform="translate(20, {header_height + qr_width + 10})">
    <text x="0" y="0" class="text-primary">License: {data.license_key}</text>
    <text x="0" y="20" class="text-secondary">Tier: {data.tier.upper()}</text>
    <text x="0" y="40" class="text-secondary">{f"Valid until: {data.expires_at}" if data.expires_at else "Lifetime validity"}</text>
  </g>
</svg>"""
        return svg_template

    def _generate_svg_compact(
        self,
        data: LicenseQRData,
        size: int
    ) -> str:
        """QR compacto con texto lateral."""
        tier_color = self._get_tier_color(data.tier)
        
        qr = self._create_qr_code(data.to_json(), box_size=8, border=2)
        factory = SvgFillImage
        img = qr.make_image(fill_color=tier_color, back_color="white", image_factory=factory)
        
        svg_bytes = io.BytesIO()
        img.save(svg_bytes)
        qr_svg = svg_bytes.getvalue().decode("utf-8")

        import re
        viewbox_match = re.search(r'viewBox="([^"]+)"', qr_svg)
        viewbox = viewbox_match.group(1) if viewbox_match else "0 0 296 296"
        paths_match = re.findall(r'(<path[^>]+/>)', qr_svg)
        paths = "\n    ".join(paths_match)

        vb_parts = viewbox.split()
        qr_size = int(vb_parts[2]) if len(vb_parts) > 2 else 296

        svg_template = f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {qr_size + 180} {qr_size}" width="{size}" height="{int(size * qr_size / (qr_size + 180))}">
  <defs>
    <style>
      .qr-fill {{ fill: {tier_color}; }}
      .text-primary {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 12px; fill: {self.BRAND_DARK}; font-weight: 600; }}
      .text-secondary {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 10px; fill: #8E8E93; }}
    </style>
  </defs>
  
  <!-- QR Code -->
  <g transform="translate(0, 0)">
    {paths}
  </g>
  
  <!-- Info -->
  <g transform="translate({qr_size + 15}, 20)">
    <text x="0" y="0" class="text-primary">{data.product}</text>
    <text x="0" y="20" class="text-primary">Key: {data.license_key}</text>
    <text x="0" y="40" class="text-secondary">Tier: {data.tier.upper()}</text>
    <text x="0" y="60" class="text-secondary">{data.expires_at or "Lifetime"}</text>
  </g>
</svg>"""
        return svg_template

    # ------------------------------------------------------------------
    # PNG Generation
    # ------------------------------------------------------------------
    def generate_png(
        self,
        license_data: LicenseQRData,
        size: int = 400,
        style: QRStyle = QRStyle.DETAILED,
        **kwargs
    ) -> bytes:
        """
        Genera QR de licencia en formato PNG.
        
        Args:
            license_data: Datos de la licencia
            size: Tamano en pixeles
            style: Estilo visual
        
        Returns:
            Bytes del PNG
        """
        tier_color = self._get_tier_color(license_data.tier)
        fill_color = kwargs.get("fill_color", tier_color)
        back_color = kwargs.get("back_color", "white")

        # Create QR image
        qr = self._create_qr_code(license_data.to_json(), box_size=10)
        
        if style == QRStyle.MINIMAL:
            img = qr.make_image(fill_color=fill_color, back_color=back_color)
            img = img.resize((size, size), Image.Resampling.LANCZOS)
        else:
            # Generate larger QR
            img = qr.make_image(fill_color=fill_color, back_color=back_color)
            
            # Convert to RGBA
            if img.mode != "RGBA":
                img = img.convert("RGBA")
            
            # Add text info for detailed styles
            if style in (QRStyle.DETAILED, QRStyle.BRANDED):
                img = self._add_text_to_png(img, license_data, style)
            
            # Resize to target
            img = img.resize(
                (size, int(size * img.height / img.width)),
                Image.Resampling.LANCZOS
            )

        # Save to bytes
        buffer = io.BytesIO()
        img.save(buffer, format="PNG", optimize=True)
        return buffer.getvalue()

    def _add_text_to_png(
        self,
        qr_img: Image.Image,
        data: LicenseQRData,
        style: QRStyle
    ) -> Image.Image:
        """Anade texto al QR para estilos detallados."""
        qr_width, qr_height = qr_img.size
        
        # Create canvas with extra space for text
        info_height = 80 if style == QRStyle.BRANDED else 60
        canvas = Image.new("RGBA", (qr_width, qr_height + info_height), (255, 255, 255, 255))
        canvas.paste(qr_img, (0, 0))

        draw = ImageDraw.Draw(canvas)
        
        # Try to load a font
        try:
            font_primary = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14)
            font_secondary = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
            font_tiny = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 9)
        except Exception:
            font_primary = ImageFont.load_default()
            font_secondary = font_primary
            font_tiny = font_primary

        tier_color = self._get_tier_color(data.tier)
        text_y = qr_height + 10
        margin = 15

        # License key
        draw.text((margin, text_y), data.license_key, fill=self.BRAND_DARK, font=font_primary)
        text_y += 22

        # Tier + expiry
        tier_text = f"Tier: {data.tier.upper()}"
        if data.expires_at:
            tier_text += f"  |  Expires: {data.expires_at}"
        else:
            tier_text += "  |  Lifetime"
        draw.text((margin, text_y), tier_text, fill="#8E8E93", font=font_secondary)
        text_y += 18

        # Product
        draw.text((margin, text_y), f"{data.product} by XNOX L.L.C", fill="#C7C7CC", font=font_tiny)

        return canvas

    # ------------------------------------------------------------------
    # Base64 utilities
    # ------------------------------------------------------------------
    def generate_svg_base64(
        self,
        license_data: LicenseQRData,
        style: QRStyle = QRStyle.DETAILED,
        size: int = 400
    ) -> str:
        """Genera QR SVG como data URI base64."""
        svg = self.generate_license_qr(license_data, style, size)
        b64 = base64.b64encode(svg.encode("utf-8")).decode("ascii")
        return f"data:image/svg+xml;base64,{b64}"

    def generate_png_base64(
        self,
        license_data: LicenseQRData,
        size: int = 400,
        style: QRStyle = QRStyle.MINIMAL
    ) -> str:
        """Genera QR PNG como data URI base64."""
        png_bytes = self.generate_png(license_data, size, style)
        b64 = base64.b64encode(png_bytes).decode("ascii")
        return f"data:image/png;base64,{b64}"

    # ------------------------------------------------------------------
    # File output
    # ------------------------------------------------------------------
    def save_svg(
        self,
        license_data: LicenseQRData,
        filepath: str,
        style: QRStyle = QRStyle.DETAILED,
        size: int = 400
    ) -> str:
        """Guarda QR SVG a archivo. Retorna la ruta."""
        svg = self.generate_license_qr(license_data, style, size)
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(svg)
        logger.info("SVG QR saved to: %s", filepath)
        return filepath

    def save_png(
        self,
        license_data: LicenseQRData,
        filepath: str,
        size: int = 400,
        style: QRStyle = QRStyle.DETAILED
    ) -> str:
        """Guarda QR PNG a archivo. Retorna la ruta."""
        png_bytes = self.generate_png(license_data, size, style)
        with open(filepath, "wb") as f:
            f.write(png_bytes)
        logger.info("PNG QR saved to: %s", filepath)
        return filepath


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    
    gen = LicenseQRGenerator()
    
    data = LicenseQRData(
        license_key="PR12-ABCD-EFGH-IJKL",
        tier="pro",
        expires_at="2025-12-31",
        customer_email="user@example.com",
        product="POWSYS365"
    )
    
    # Generate all styles
    for style in QRStyle:
        svg = gen.generate_license_qr(data, style=style, size=400)
        filename = f"/tmp/license_qr_{style.value}.svg"
        gen.save_svg(data, filename, style=style)
        print(f"Generated: {filename}")
    
    # Generate PNG
    png_path = "/tmp/license_qr.png"
    gen.save_png(data, png_path, size=400)
    print(f"Generated: {png_path}")
