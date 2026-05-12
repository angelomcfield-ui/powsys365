#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
POWSYS365 - PayPal REST API v2 Client
Cliente completo para integracion con PayPal REST API v2.

Funcionalidades:
- Autenticacion OAuth2
- Creacion de planes de suscripcion
- Creacion y gestion de suscripciones
- Pagos unicos (Orders API)
- Verificacion de webhooks
- Reembolsos

Requiere:
    requests>=2.28.0
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import logging
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Dict, List, Optional, Tuple

import requests

logger = logging.getLogger("powsys365.paypal")


class PayPalEnvironment(Enum):
    """Entornos de PayPal."""
    SANDBOX = "sandbox"
    LIVE = "live"


class SubscriptionStatus(Enum):
    """Estados de suscripcion PayPal."""
    APPROVAL_PENDING = "APPROVAL_PENDING"
    APPROVED = "APPROVED"
    ACTIVE = "ACTIVE"
    SUSPENDED = "SUSPENDED"
    CANCELLED = "CANCELLED"
    EXPIRED = "EXPIRED"


@dataclass
class PayPalConfig:
    """Configuracion del cliente PayPal."""
    client_id: str
    client_secret: str
    environment: PayPalEnvironment = PayPalEnvironment.SANDBOX
    webhook_id: str = ""
    app_id: str = ""

    @property
    def base_url(self) -> str:
        if self.environment == PayPalEnvironment.LIVE:
            return "https://api.paypal.com"
        return "https://api.sandbox.paypal.com"


@dataclass
class SubscriptionPlan:
    """Plan de suscripcion PayPal."""
    plan_id: str
    product_id: str
    name: str
    description: str
    status: str
    billing_cycles: List[Dict[str, Any]] = field(default_factory=list)
    create_time: str = ""


@dataclass
class Subscription:
    """Suscripcion PayPal activa."""
    subscription_id: str
    plan_id: str
    status: str
    subscriber_email: str = ""
    subscriber_name: str = ""
    billing_info: Dict[str, Any] = field(default_factory=dict)
    create_time: str = ""
    start_time: str = ""


@dataclass
class WebhookEvent:
    """Evento de webhook PayPal."""
    event_id: str
    event_type: str
    resource_type: str
    resource: Dict[str, Any] = field(default_factory=dict)
    create_time: str = ""
    summary: str = ""


class PayPalError(Exception):
    """Error de la API de PayPal."""
    def __init__(self, message: str, status_code: int = 0, details: Optional[Dict] = None):
        super().__init__(message)
        self.status_code = status_code
        self.details = details or {}


class PayPalClient:
    """
    Cliente PayPal REST API v2 completo.
    
    Usage:
        config = PayPalConfig(
            client_id="YOUR_CLIENT_ID",
            client_secret="YOUR_SECRET",
            environment=PayPalEnvironment.SANDBOX
        )
        client = PayPalClient(config)
        
        # Crear plan
        plan = client.create_subscription_plan(
            name="POWSYS365 Pro",
            description="Power Systems Analysis Suite - Pro Tier",
            price=799.00,
            currency="USD",
            interval_unit="MONTH",
            interval_count=1
        )
    """

    def __init__(self, config: PayPalConfig):
        self.config = config
        self._access_token: Optional[str] = None
        self._token_expires_at: float = 0.0
        self._session = requests.Session()
        self._session.headers.update({
            "Content-Type": "application/json",
            "Accept": "application/json",
        })

    # ------------------------------------------------------------------
    # Auth
    # ------------------------------------------------------------------
    def _get_access_token(self) -> str:
        """Obtiene o renueva el token de acceso OAuth2."""
        now = time.time()
        if self._access_token and now < self._token_expires_at - 60:
            return self._access_token

        credentials = base64.b64encode(
            f"{self.config.client_id}:{self.config.client_secret}".encode()
        ).decode()

        response = self._session.post(
            f"{self.config.base_url}/v1/oauth2/token",
            headers={"Authorization": f"Basic {credentials}"},
            data={"grant_type": "client_credentials"},
            timeout=30
        )

        if response.status_code != 200:
            raise PayPalError(
                f"Auth failed: {response.status_code} - {response.text}",
                response.status_code
            )

        data = response.json()
        self._access_token = data["access_token"]
        expires_in = data.get("expires_in", 32400)
        self._token_expires_at = now + expires_in

        # Update session headers
        self._session.headers["Authorization"] = f"Bearer {self._access_token}"

        logger.info("PayPal access token obtained, expires in %ds", expires_in)
        return self._access_token

    def _ensure_auth(self) -> None:
        """Asegura que tenemos un token valido."""
        self._get_access_token()

    def _request(
        self,
        method: str,
        endpoint: str,
        json_data: Optional[Dict] = None,
        params: Optional[Dict] = None,
        headers: Optional[Dict] = None,
        timeout: int = 30
    ) -> Dict[str, Any]:
        """Realiza una peticion autenticada a la API."""
        self._ensure_auth()

        url = f"{self.config.base_url}{endpoint}"
        request_headers = dict(self._session.headers)
        if headers:
            request_headers.update(headers)

        response = self._session.request(
            method=method,
            url=url,
            json=json_data,
            params=params,
            headers=request_headers,
            timeout=timeout
        )

        if response.status_code >= 400:
            error_detail = {}
            try:
                error_detail = response.json()
            except Exception:
                pass
            raise PayPalError(
                f"PayPal API error {response.status_code}: {response.text}",
                response.status_code,
                error_detail
            )

        if response.status_code == 204:
            return {}

        try:
            return response.json()
        except Exception:
            return {"raw_response": response.text}

    # ------------------------------------------------------------------
    # Products
    # ------------------------------------------------------------------
    def create_product(
        self,
        name: str,
        description: str,
        product_type: str = "SERVICE",
        category: str = "SOFTWARE",
        image_url: str = "",
        home_url: str = ""
    ) -> Dict[str, Any]:
        """
        Crea un producto en el catalogo de PayPal.
        
        Args:
            name: Nombre del producto
            description: Descripcion
            product_type: Tipo (SERVICE, PHYSICAL, DIGITAL)
            category: Categoria
        """
        payload: Dict[str, Any] = {
            "name": name,
            "description": description,
            "type": product_type,
            "category": category,
        }
        if image_url:
            payload["image_url"] = image_url
        if home_url:
            payload["home_url"] = home_url

        result = self._request("POST", "/v1/catalogs/products", payload)
        logger.info("Product created: %s", result.get("id"))
        return result

    def get_product(self, product_id: str) -> Dict[str, Any]:
        """Obtiene un producto por ID."""
        return self._request("GET", f"/v1/catalogs/products/{product_id}")

    def list_products(self, page_size: int = 10, page: int = 1) -> List[Dict[str, Any]]:
        """Lista productos del catalogo."""
        result = self._request(
            "GET", "/v1/catalogs/products",
            params={"page_size": page_size, "page": page}
        )
        return result.get("products", [])

    # ------------------------------------------------------------------
    # Subscription Plans
    # ------------------------------------------------------------------
    def create_subscription_plan(
        self,
        product_id: str,
        name: str,
        description: str,
        price: float,
        currency: str = "USD",
        interval_unit: str = "MONTH",
        interval_count: int = 1,
        total_cycles: int = 0,  # 0 = unlimited
        setup_fee: float = 0.0,
        auto_bill_outstanding: bool = True,
        payment_failure_threshold: int = 3,
    ) -> Dict[str, Any]:
        """
        Crea un plan de suscripcion.
        
        Args:
            product_id: ID del producto asociado
            name: Nombre del plan
            price: Precio por ciclo
            currency: Moneda (USD, EUR, etc.)
            interval_unit: DAY, WEEK, MONTH, YEAR
            interval_count: Cada cuantas unidades
            total_cycles: 0 = ilimitado
            setup_fee: Tarifa de configuracion
        """
        price_str = f"{price:.2f}"

        billing_cycle = {
            "frequency": {
                "interval_unit": interval_unit,
                "interval_count": interval_count
            },
            "tenure_type": "REGULAR",
            "sequence": 1,
            "total_cycles": total_cycles,
            "pricing_scheme": {
                "fixed_price": {
                    "value": price_str,
                    "currency_code": currency
                }
            }
        }

        payload: Dict[str, Any] = {
            "product_id": product_id,
            "name": name,
            "description": description,
            "billing_cycles": [billing_cycle],
            "payment_preferences": {
                "auto_bill_outstanding": auto_bill_outstanding,
                "setup_fee_failure_action": "CONTINUE",
                "payment_failure_threshold": payment_failure_threshold,
            },
        }

        if setup_fee > 0:
            payload["payment_preferences"]["setup_fee"] = {
                "value": f"{setup_fee:.2f}",
                "currency_code": currency
            }

        result = self._request("POST", "/v1/billing/plans", payload)
        logger.info("Subscription plan created: %s", result.get("id"))
        return result

    def get_subscription_plan(self, plan_id: str) -> Dict[str, Any]:
        """Obtiene detalles de un plan."""
        return self._request("GET", f"/v1/billing/plans/{plan_id}")

    def list_subscription_plans(
        self,
        product_id: str = "",
        page_size: int = 10,
        page: int = 1,
        plan_ids: Optional[List[str]] = None
    ) -> List[Dict[str, Any]]:
        """Lista planes de suscripcion."""
        params: Dict[str, Any] = {"page_size": page_size, "page": page}
        if product_id:
            params["product_id"] = product_id
        if plan_ids:
            params["plan_ids"] = ",".join(plan_ids)

        result = self._request("GET", "/v1/billing/plans", params=params)
        return result.get("plans", [])

    def update_subscription_plan(
        self,
        plan_id: str,
        operations: List[Dict[str, Any]]
    ) -> Dict[str, Any]:
        """
        Actualiza un plan parcialmente (JSON Patch).
        
        Args:
            operations: Lista de operaciones JSON Patch
        """
        return self._request("PATCH", f"/v1/billing/plans/{plan_id}", operations)

    def deactivate_subscription_plan(self, plan_id: str) -> Dict[str, Any]:
        """Desactiva un plan."""
        return self._request("POST", f"/v1/billing/plans/{plan_id}/deactivate")

    def activate_subscription_plan(self, plan_id: str) -> Dict[str, Any]:
        """Activa un plan previamente desactivado."""
        return self._request("POST", f"/v1/billing/plans/{plan_id}/activate")

    # ------------------------------------------------------------------
    # Subscriptions
    # ------------------------------------------------------------------
    def create_subscription(
        self,
        plan_id: str,
        subscriber_email: str = "",
        subscriber_name: str = "",
        return_url: str = "",
        cancel_url: str = "",
        custom_id: str = "",
    ) -> Dict[str, Any]:
        """
        Crea una nueva suscripcion.
        
        Returns:
            Dict con subscription_id y links de aprobacion
        """
        payload: Dict[str, Any] = {
            "plan_id": plan_id,
            "application_context": {
                "brand_name": "XNOX L.L.C - POWSYS365",
                "locale": "en-US",
                "shipping_preference": "NO_SHIPPING",
                "user_action": "SUBSCRIBE_NOW",
            }
        }

        if subscriber_email or subscriber_name:
            payload["subscriber"] = {}
            if subscriber_name:
                payload["subscriber"]["name"] = {"given_name": subscriber_name}
            if subscriber_email:
                payload["subscriber"]["email_address"] = subscriber_email

        if return_url:
            payload["application_context"]["return_url"] = return_url
        if cancel_url:
            payload["application_context"]["cancel_url"] = cancel_url
        if custom_id:
            payload["custom_id"] = custom_id

        result = self._request("POST", "/v1/billing/subscriptions", payload)
        logger.info("Subscription created: %s", result.get("id"))
        return result

    def get_subscription(self, subscription_id: str) -> Dict[str, Any]:
        """Obtiene detalles de una suscripcion."""
        return self._request("GET", f"/v1/billing/subscriptions/{subscription_id}")

    def cancel_subscription(
        self,
        subscription_id: str,
        reason: str = "User requested cancellation"
    ) -> bool:
        """
        Cancela una suscripcion.
        
        Returns:
            True si la cancelacion fue exitosa
        """
        try:
            self._request(
                "POST",
                f"/v1/billing/subscriptions/{subscription_id}/cancel",
                {"reason": reason}
            )
            logger.info("Subscription cancelled: %s", subscription_id)
            return True
        except PayPalError as e:
            logger.error("Failed to cancel subscription %s: %s", subscription_id, e)
            return False

    def suspend_subscription(
        self,
        subscription_id: str,
        reason: str = "User requested suspension"
    ) -> bool:
        """Suspende una suscripcion."""
        try:
            self._request(
                "POST",
                f"/v1/billing/subscriptions/{subscription_id}/suspend",
                {"reason": reason}
            )
            logger.info("Subscription suspended: %s", subscription_id)
            return True
        except PayPalError:
            return False

    def activate_subscription(self, subscription_id: str) -> bool:
        """Reactiva una suscripcion suspendida."""
        try:
            self._request(
                "POST",
                f"/v1/billing/subscriptions/{subscription_id}/activate"
            )
            logger.info("Subscription activated: %s", subscription_id)
            return True
        except PayPalError:
            return False

    def update_subscription_quantity(
        self,
        subscription_id: str,
        quantity: int,
        reason: str = ""
    ) -> Dict[str, Any]:
        """Actualiza la cantidad de una suscripcion."""
        operations = [
            {
                "op": "replace",
                "path": "/quantity",
                "value": str(quantity)
            }
        ]
        if reason:
            operations.append({
                "op": "add",
                "path": "/reason",
                "value": reason
            })

        return self._request(
            "PATCH",
            f"/v1/billing/subscriptions/{subscription_id}",
            operations
        )

    def list_transactions(
        self,
        subscription_id: str,
        start_time: str,
        end_time: str
    ) -> List[Dict[str, Any]]:
        """Lista transacciones de una suscripcion."""
        result = self._request(
            "GET",
            f"/v1/billing/subscriptions/{subscription_id}/transactions",
            params={"start_time": start_time, "end_time": end_time}
        )
        return result.get("transactions", [])

    # ------------------------------------------------------------------
    # One-Time Payments (Orders API)
    # ------------------------------------------------------------------
    def create_order(
        self,
        amount: float,
        currency: str = "USD",
        description: str = "",
        return_url: str = "",
        cancel_url: str = "",
        custom_id: str = "",
    ) -> Dict[str, Any]:
        """
        Crea una orden de pago unico.
        
        Returns:
            Dict con order_id y approval_url
        """
        payload = {
            "intent": "CAPTURE",
            "purchase_units": [
                {
                    "amount": {
                        "currency_code": currency,
                        "value": f"{amount:.2f}"
                    },
                    "description": description or "POWSYS365 Purchase",
                }
            ],
            "application_context": {
                "brand_name": "XNOX L.L.C - POWSYS365",
                "shipping_preference": "NO_SHIPPING",
                "user_action": "PAY_NOW",
            }
        }

        if custom_id:
            payload["purchase_units"][0]["custom_id"] = custom_id
        if return_url:
            payload["application_context"]["return_url"] = return_url
        if cancel_url:
            payload["application_context"]["cancel_url"] = cancel_url

        result = self._request("POST", "/v2/checkout/orders", payload)
        logger.info("Order created: %s", result.get("id"))
        return result

    def capture_order(self, order_id: str) -> Dict[str, Any]:
        """Captura un pago de una orden aprobada."""
        result = self._request(
            "POST",
            f"/v2/checkout/orders/{order_id}/capture"
        )
        logger.info("Order captured: %s", order_id)
        return result

    def get_order(self, order_id: str) -> Dict[str, Any]:
        """Obtiene detalles de una orden."""
        return self._request("GET", f"/v2/checkout/orders/{order_id}")

    def refund_order(
        self,
        capture_id: str,
        amount: float = 0.0,
        currency: str = "USD",
        reason: str = ""
    ) -> Dict[str, Any]:
        """Reembolsa una captura."""
        payload: Dict[str, Any] = {}
        if amount > 0:
            payload["amount"] = {
                "value": f"{amount:.2f}",
                "currency_code": currency
            }
        if reason:
            payload["note_to_payer"] = reason

        return self._request(
            "POST",
            f"/v2/payments/captures/{capture_id}/refund",
            payload
        )

    # ------------------------------------------------------------------
    # Webhook Verification
    # ------------------------------------------------------------------
    def verify_webhook(
        self,
        transmission_id: str,
        cert_url: str,
        auth_algo: str,
        transmission_sig: str,
        webhook_body: str
    ) -> bool:
        """
        Verifica la firma de un webhook de PayPal.
        
        PayPal envia estos headers:
            PAYPAL-TRANSMISSION-ID
            PAYPAL-CERT-URL
            PAYPAL-AUTH-ALGO
            PAYPAL-TRANSMISSION-SIG
        
        Args:
            transmission_id: Header PAYPAL-TRANSMISSION-ID
            cert_url: Header PAYPAL-CERT-URL
            auth_algo: Header PAYPAL-AUTH-ALGO
            transmission_sig: Header PAYPAL-TRANSMISSION-SIG
            webhook_body: Cuerpo raw del webhook
        
        Returns:
            True si la firma es valida
        """
        if not self.config.webhook_id:
            logger.warning("Webhook verification skipped: no webhook_id configured")
            return True

        payload = {
            "transmission_id": transmission_id,
            "cert_url": cert_url,
            "auth_algo": auth_algo,
            "transmission_sig": transmission_sig,
            "webhook_id": self.config.webhook_id,
            "webhook_event": json.loads(webhook_body) if webhook_body else {}
        }

        try:
            result = self._request(
                "POST",
                "/v1/notifications/verify-webhook-signature",
                payload
            )
            verified = result.get("verification_status") == "SUCCESS"
            logger.info("Webhook verification: %s", verified)
            return verified
        except PayPalError as e:
            logger.error("Webhook verification failed: %s", e)
            return False

    def get_webhook_event(self, event_id: str) -> Dict[str, Any]:
        """Obtiene un evento de webhook por ID."""
        return self._request("GET", f"/v1/notifications/webhooks-events/{event_id}")

    def list_webhook_events(
        self,
        event_types: Optional[List[str]] = None,
        start_time: str = "",
        end_time: str = ""
    ) -> List[Dict[str, Any]]:
        """Lista eventos de webhook."""
        params: Dict[str, Any] = {}
        if event_types:
            params["event_type"] = ",".join(event_types)
        if start_time:
            params["start_time"] = start_time
        if end_time:
            params["end_time"] = end_time

        result = self._request("GET", "/v1/notifications/webhooks-events", params=params)
        return result.get("events", [])

    # ------------------------------------------------------------------
    # Utility
    # ------------------------------------------------------------------
    def health_check(self) -> Dict[str, Any]:
        """Verifica la conectividad con PayPal."""
        try:
            self._ensure_auth()
            return {
                "status": "healthy",
                "authenticated": True,
                "environment": self.config.environment.value,
                "token_expires_at": datetime.fromtimestamp(
                    self._token_expires_at, tz=timezone.utc
                ).isoformat() if self._token_expires_at else None
            }
        except PayPalError as e:
            return {
                "status": "unhealthy",
                "error": str(e),
                "environment": self.config.environment.value
            }
