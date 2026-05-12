#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
POWSYS365 - Stripe API Client
Cliente completo para integracion con Stripe API.

Funcionalidades:
- Customer management
- Subscription lifecycle
- Checkout sessions
- Payment intents
- Webhook verification
- Refunds and invoicing

Requiere:
    stripe>=5.0.0
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Dict, List, Optional, Union

import stripe

logger = logging.getLogger("powsys365.stripe")


class SubscriptionTier(Enum):
    """Niveles de suscripcion."""
    BASIC = "basic"
    PRO = "pro"
    ENTERPRISE = "enterprise"
    LIFETIME = "lifetime"


class SubscriptionInterval(Enum):
    """Intervalos de facturacion."""
    MONTH = "month"
    YEAR = "year"


@dataclass
class StripeConfig:
    """Configuracion del cliente Stripe."""
    secret_key: str
    publishable_key: str = ""
    webhook_secret: str = ""
    api_version: str = "2023-10-16"


@dataclass
class CustomerData:
    """Datos del cliente."""
    email: str
    name: str = ""
    phone: str = ""
    description: str = ""
    metadata: Dict[str, str] = field(default_factory=dict)


@dataclass
class SubscriptionData:
    """Datos de suscripcion."""
    customer_id: str
    price_id: str
    status: str = ""
    current_period_start: int = 0
    current_period_end: int = 0
    cancel_at_period_end: bool = False


class StripeError(Exception):
    """Error de la API de Stripe."""
    def __init__(
        self,
        message: str,
        code: str = "",
        param: str = "",
        type_: str = "",
        status_code: int = 0
    ):
        super().__init__(message)
        self.code = code
        self.param = param
        self.type = type_
        self.status_code = status_code


class StripeClient:
    """
    Cliente Stripe completo para POWSYS365.
    
    Usage:
        config = StripeConfig(secret_key="sk_test_...")
        client = StripeClient(config)
        
        # Crear cliente
        customer = client.create_customer("user@example.com", "John Doe")
        
        # Crear checkout session
        session = client.create_checkout_session(
            customer_id=customer["id"],
            price_data={
                "unit_amount": 79900,  # $799.00 in cents
                "currency": "usd",
                "product_data": {"name": "POWSYS365 Pro"},
            },
            mode="subscription"
        )
    """

    # Pricing tiers in cents
    PRICES = {
        (SubscriptionTier.BASIC, SubscriptionInterval.MONTH): 29900,
        (SubscriptionTier.BASIC, SubscriptionInterval.YEAR): 299000,
        (SubscriptionTier.PRO, SubscriptionInterval.MONTH): 79900,
        (SubscriptionTier.PRO, SubscriptionInterval.YEAR): 799000,
        (SubscriptionTier.ENTERPRISE, SubscriptionInterval.MONTH): 249900,
        (SubscriptionTier.ENTERPRISE, SubscriptionInterval.YEAR): 2499000,
        (SubscriptionTier.LIFETIME, SubscriptionInterval.YEAR): 499900,
    }

    def __init__(self, config: StripeConfig):
        self.config = config
        stripe.api_key = config.secret_key
        stripe.api_version = config.api_version
        self._stripe = stripe

    def _handle_error(self, error: stripe.error.StripeError) -> None:
        """Convierte errores de Stripe a excepciones propias."""
        body = error.json_body or {}
        err = body.get("error", {})
        raise StripeError(
            message=str(error),
            code=err.get("code", ""),
            param=err.get("param", ""),
            type_=err.get("type", ""),
            status_code=getattr(error, "http_status", 0)
        )

    # ------------------------------------------------------------------
    # Customers
    # ------------------------------------------------------------------
    def create_customer(
        self,
        email: str,
        name: str = "",
        phone: str = "",
        description: str = "",
        metadata: Optional[Dict[str, str]] = None,
    ) -> Dict[str, Any]:
        """
        Crea un cliente en Stripe.
        
        Args:
            email: Correo electronico del cliente
            name: Nombre completo
            phone: Telefono
            description: Descripcion interna
            metadata: Metadatos personalizados
        
        Returns:
            Objeto Customer de Stripe
        """
        try:
            params: Dict[str, Any] = {"email": email}
            if name:
                params["name"] = name
            if phone:
                params["phone"] = phone
            if description:
                params["description"] = description
            if metadata:
                params["metadata"] = metadata

            customer = self._stripe.Customer.create(**params)
            logger.info("Customer created: %s (%s)", customer.id, email)
            return dict(customer)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}  # never reached

    def get_customer(self, customer_id: str) -> Dict[str, Any]:
        """Obtiene un cliente por ID."""
        try:
            customer = self._stripe.Customer.retrieve(customer_id)
            return dict(customer)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def update_customer(
        self,
        customer_id: str,
        email: str = "",
        name: str = "",
        phone: str = "",
        metadata: Optional[Dict[str, str]] = None,
    ) -> Dict[str, Any]:
        """Actualiza un cliente."""
        try:
            params: Dict[str, Any] = {}
            if email:
                params["email"] = email
            if name:
                params["name"] = name
            if phone:
                params["phone"] = phone
            if metadata:
                params["metadata"] = metadata

            customer = self._stripe.Customer.modify(customer_id, **params)
            logger.info("Customer updated: %s", customer_id)
            return dict(customer)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def delete_customer(self, customer_id: str) -> bool:
        """Elimina un cliente."""
        try:
            self._stripe.Customer.delete(customer_id)
            logger.info("Customer deleted: %s", customer_id)
            return True
        except stripe.error.StripeError as e:
            logger.error("Failed to delete customer %s: %s", customer_id, e)
            return False

    def list_customers(
        self,
        email: str = "",
        limit: int = 10,
        starting_after: str = ""
    ) -> List[Dict[str, Any]]:
        """Lista clientes."""
        try:
            params: Dict[str, Any] = {"limit": limit}
            if email:
                params["email"] = email
            if starting_after:
                params["starting_after"] = starting_after

            customers = self._stripe.Customer.list(**params)
            return [dict(c) for c in customers.data]
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return []

    # ------------------------------------------------------------------
    # Subscriptions
    # ------------------------------------------------------------------
    def create_subscription(
        self,
        customer_id: str,
        price_id: str,
        metadata: Optional[Dict[str, str]] = None,
        trial_period_days: int = 0,
        cancel_at_period_end: bool = False,
        default_payment_method: str = "",
    ) -> Dict[str, Any]:
        """
        Crea una suscripcion.
        
        Args:
            customer_id: ID del cliente
            price_id: ID del precio (debe existir en Stripe)
            trial_period_days: Dias de prueba gratuita
            cancel_at_period_end: Cancelar al final del periodo
        """
        try:
            params: Dict[str, Any] = {
                "customer": customer_id,
                "items": [{"price": price_id}],
            }
            if metadata:
                params["metadata"] = metadata
            if trial_period_days > 0:
                params["trial_period_days"] = trial_period_days
            if cancel_at_period_end:
                params["cancel_at_period_end"] = True
            if default_payment_method:
                params["default_payment_method"] = default_payment_method

            subscription = self._stripe.Subscription.create(**params)
            logger.info(
                "Subscription created: %s (customer: %s, price: %s)",
                subscription.id, customer_id, price_id
            )
            return dict(subscription)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def get_subscription(self, subscription_id: str) -> Dict[str, Any]:
        """Obtiene detalles de una suscripcion."""
        try:
            sub = self._stripe.Subscription.retrieve(subscription_id)
            return dict(sub)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def get_subscription_status(self, subscription_id: str) -> str:
        """
        Obtiene el estado de una suscripcion.
        
        Returns:
            Estado: 'active', 'canceled', 'incomplete', 'past_due', etc.
        """
        try:
            sub = self._stripe.Subscription.retrieve(subscription_id)
            return sub.status
        except stripe.error.StripeError:
            return "unknown"

    def cancel_subscription(
        self,
        subscription_id: str,
        immediately: bool = False
    ) -> Dict[str, Any]:
        """
        Cancela una suscripcion.
        
        Args:
            subscription_id: ID de la suscripcion
            immediately: Si True, cancela inmediatamente; si False, al final del periodo
        """
        try:
            if immediately:
                sub = self._stripe.Subscription.delete(subscription_id)
            else:
                sub = self._stripe.Subscription.modify(
                    subscription_id,
                    cancel_at_period_end=True
                )
            logger.info("Subscription %s cancelled", subscription_id)
            return dict(sub)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def reactivate_subscription(self, subscription_id: str) -> Dict[str, Any]:
        """Reactiva una suscripcion que iba a cancelarse al final del periodo."""
        try:
            sub = self._stripe.Subscription.modify(
                subscription_id,
                cancel_at_period_end=False
            )
            logger.info("Subscription %s reactivated", subscription_id)
            return dict(sub)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def update_subscription(
        self,
        subscription_id: str,
        price_id: str = "",
        quantity: int = 0,
        proration_behavior: str = "create_prorations"
    ) -> Dict[str, Any]:
        """
        Actualiza una suscripcion (cambio de plan o cantidad).
        """
        try:
            params: Dict[str, Any] = {
                "proration_behavior": proration_behavior
            }
            if price_id:
                params["items"] = [{"price": price_id}]
                if quantity > 0:
                    params["items"][0]["quantity"] = quantity
            elif quantity > 0:
                params["quantity"] = quantity

            sub = self._stripe.Subscription.modify(subscription_id, **params)
            return dict(sub)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def list_subscriptions(
        self,
        customer_id: str = "",
        status: str = "",
        limit: int = 10
    ) -> List[Dict[str, Any]]:
        """Lista suscripciones."""
        try:
            params: Dict[str, Any] = {"limit": limit}
            if customer_id:
                params["customer"] = customer_id
            if status:
                params["status"] = status

            subs = self._stripe.Subscription.list(**params)
            return [dict(s) for s in subs.data]
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return []

    # ------------------------------------------------------------------
    # Checkout Sessions
    # ------------------------------------------------------------------
    def create_checkout_session(
        self,
        customer_id: str = "",
        customer_email: str = "",
        price_id: str = "",
        price_data: Optional[Dict[str, Any]] = None,
        mode: str = "subscription",  # 'subscription' or 'payment'
        success_url: str = "",
        cancel_url: str = "",
        metadata: Optional[Dict[str, str]] = None,
        allow_promotion_codes: bool = False,
        client_reference_id: str = "",
    ) -> Dict[str, Any]:
        """
        Crea una sesion de checkout (Stripe Hosted).
        
        Args:
            customer_id: ID del cliente existente (opcional)
            customer_email: Email para prellenar
            price_id: ID del precio existente
            price_data: Datos para crear precio ad-hoc
            mode: 'subscription', 'payment', o 'setup'
            success_url: URL de redireccion exitosa
            cancel_url: URL de redireccion cancelada
        """
        try:
            params: Dict[str, Any] = {
                "mode": mode,
                "success_url": success_url or "https://xnovatech.com/payment/success?session_id={CHECKOUT_SESSION_ID}",
                "cancel_url": cancel_url or "https://xnovatech.com/payment/cancel",
                "line_items": [{}],
            }

            # Customer
            if customer_id:
                params["customer"] = customer_id
            elif customer_email:
                params["customer_email"] = customer_email

            # Pricing
            if price_id:
                params["line_items"][0]["price"] = price_id
                params["line_items"][0]["quantity"] = 1
            elif price_data:
                params["line_items"][0]["price_data"] = price_data
                params["line_items"][0]["quantity"] = 1

            # Options
            if metadata:
                params["metadata"] = metadata
            if allow_promotion_codes:
                params["allow_promotion_codes"] = True
            if client_reference_id:
                params["client_reference_id"] = client_reference_id

            # Adapt for payment mode
            if mode == "payment":
                params.pop("line_items", None)
                params["line_items"] = []
                if price_id:
                    params["line_items"].append({
                        "price": price_id,
                        "quantity": 1
                    })
                elif price_data:
                    params["line_items"].append({
                        "price_data": price_data,
                        "quantity": 1
                    })

            session = self._stripe.checkout.Session.create(**params)
            logger.info(
                "Checkout session created: %s (mode: %s)",
                session.id, mode
            )
            return dict(session)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def get_checkout_session(self, session_id: str) -> Dict[str, Any]:
        """Obtiene una sesion de checkout."""
        try:
            session = self._stripe.checkout.Session.retrieve(session_id)
            return dict(session)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def expire_checkout_session(self, session_id: str) -> Dict[str, Any]:
        """Expira una sesion de checkout activa."""
        try:
            session = self._stripe.checkout.Session.expire(session_id)
            return dict(session)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    # ------------------------------------------------------------------
    # Payment Intents
    # ------------------------------------------------------------------
    def create_payment_intent(
        self,
        amount: int,  # in cents
        currency: str = "usd",
        customer_id: str = "",
        description: str = "",
        metadata: Optional[Dict[str, str]] = None,
        receipt_email: str = "",
        automatic_payment_methods: bool = True,
    ) -> Dict[str, Any]:
        """
        Crea un PaymentIntent para pagos programaticos.
        
        Args:
            amount: Monto en centavos (ej: 79900 = $799.00)
        """
        try:
            params: Dict[str, Any] = {
                "amount": amount,
                "currency": currency,
            }
            if automatic_payment_methods:
                params["automatic_payment_methods"] = {"enabled": True}
            if customer_id:
                params["customer"] = customer_id
            if description:
                params["description"] = description
            if metadata:
                params["metadata"] = metadata
            if receipt_email:
                params["receipt_email"] = receipt_email

            intent = self._stripe.PaymentIntent.create(**params)
            logger.info("PaymentIntent created: %s", intent.id)
            return dict(intent)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def confirm_payment_intent(
        self,
        payment_intent_id: str,
        payment_method: str = "",
    ) -> Dict[str, Any]:
        """Confirma un PaymentIntent."""
        try:
            params: Dict[str, Any] = {}
            if payment_method:
                params["payment_method"] = payment_method

            intent = self._stripe.PaymentIntent.confirm(payment_intent_id, **params)
            return dict(intent)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def capture_payment_intent(
        self,
        payment_intent_id: str,
        amount: int = 0,
    ) -> Dict[str, Any]:
        """Captura un PaymentIntent."""
        try:
            params: Dict[str, Any] = {}
            if amount > 0:
                params["amount_to_capture"] = amount

            intent = self._stripe.PaymentIntent.capture(payment_intent_id, **params)
            return dict(intent)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    # ------------------------------------------------------------------
    # Refunds
    # ------------------------------------------------------------------
    def create_refund(
        self,
        payment_intent_id: str = "",
        charge_id: str = "",
        amount: int = 0,  # cents, 0 = full refund
        reason: str = "",  # 'duplicate', 'fraudulent', 'requested_by_customer'
    ) -> Dict[str, Any]:
        """Crea un reembolso."""
        try:
            params: Dict[str, Any] = {}
            if payment_intent_id:
                params["payment_intent"] = payment_intent_id
            elif charge_id:
                params["charge"] = charge_id
            if amount > 0:
                params["amount"] = amount
            if reason:
                params["reason"] = reason

            refund = self._stripe.Refund.create(**params)
            logger.info("Refund created: %s", refund.id)
            return dict(refund)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    # ------------------------------------------------------------------
    # Invoices
    # ------------------------------------------------------------------
    def get_invoice(self, invoice_id: str) -> Dict[str, Any]:
        """Obtiene una factura."""
        try:
            invoice = self._stripe.Invoice.retrieve(invoice_id)
            return dict(invoice)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def list_invoices(
        self,
        customer_id: str = "",
        subscription_id: str = "",
        limit: int = 10
    ) -> List[Dict[str, Any]]:
        """Lista facturas."""
        try:
            params: Dict[str, Any] = {"limit": limit}
            if customer_id:
                params["customer"] = customer_id
            if subscription_id:
                params["subscription"] = subscription_id

            invoices = self._stripe.Invoice.list(**params)
            return [dict(i) for i in invoices.data]
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return []

    # ------------------------------------------------------------------
    # Webhook Verification
    # ------------------------------------------------------------------
    def construct_webhook_event(
        self,
        payload: bytes,
        sig_header: str,
        secret: str = "",
        tolerance: int = 300
    ) -> Dict[str, Any]:
        """
        Construye y verifica un evento de webhook.
        
        Verifica la firma del webhook usando el secreto del endpoint.
        Lanza excepcion si la firma es invalida.
        
        Args:
            payload: Cuerpo raw del request (bytes)
            sig_header: Valor del header 'Stripe-Signature'
            secret: Webhook endpoint secret (usa config.webhook_secret si vacio)
            tolerance: Tolerancia en segundos para el timestamp
        
        Returns:
            Evento verificado como dict
        
        Raises:
            StripeError: Si la firma es invalida
        """
        webhook_secret = secret or self.config.webhook_secret
        if not webhook_secret:
            logger.warning("No webhook secret configured, skipping verification")
            import json
            return json.loads(payload)

        try:
            event = self._stripe.Webhook.construct_event(
                payload, sig_header, webhook_secret, tolerance
            )
            logger.info("Webhook verified: %s (type: %s)", event.id, event.type)
            return dict(event)
        except stripe.error.SignatureVerificationError as e:
            logger.error("Webhook signature verification failed: %s", e)
            raise StripeError(
                f"Invalid webhook signature: {e}",
                type_="signature_verification_failed"
            )
        except Exception as e:
            logger.error("Webhook processing error: %s", e)
            raise StripeError(f"Webhook error: {e}")

    # ------------------------------------------------------------------
    # Prices & Products helpers
    # ------------------------------------------------------------------
    def create_price(
        self,
        product_id: str = "",
        product_name: str = "",
        unit_amount: int = 0,  # cents
        currency: str = "usd",
        interval: str = "",  # 'month' or 'year' for recurring
        interval_count: int = 1,
        metadata: Optional[Dict[str, str]] = None,
    ) -> Dict[str, Any]:
        """
        Crea un objeto Price.
        
        Si product_id no se provee, crea un producto nuevo con product_name.
        """
        try:
            params: Dict[str, Any] = {
                "unit_amount": unit_amount,
                "currency": currency,
            }

            if product_id:
                params["product"] = product_id
            elif product_name:
                # Create product on the fly
                product = self._stripe.Product.create(name=product_name)
                params["product"] = product.id

            if interval:
                params["recurring"] = {
                    "interval": interval,
                    "interval_count": interval_count
                }

            if metadata:
                params["metadata"] = metadata

            price = self._stripe.Price.create(**params)
            logger.info("Price created: %s", price.id)
            return dict(price)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    def get_price(self, price_id: str) -> Dict[str, Any]:
        """Obtiene un precio por ID."""
        try:
            price = self._stripe.Price.retrieve(price_id)
            return dict(price)
        except stripe.error.StripeError as e:
            self._handle_error(e)
            return {}

    # ------------------------------------------------------------------
    # Utility
    # ------------------------------------------------------------------
    def health_check(self) -> Dict[str, Any]:
        """Verifica la conectividad con Stripe."""
        try:
            # List customers with limit 1 to verify API key
            self._stripe.Customer.list(limit=1)
            return {
                "status": "healthy",
                "authenticated": True,
                "api_version": self.config.api_version,
            }
        except stripe.error.AuthenticationError as e:
            return {
                "status": "unhealthy",
                "error": "Invalid API key",
                "authenticated": False,
            }
        except stripe.error.StripeError as e:
            return {
                "status": "degraded",
                "error": str(e),
                "authenticated": True,
            }

    @staticmethod
    def tier_to_price_cents(
        tier: SubscriptionTier,
        interval: SubscriptionInterval
    ) -> int:
        """Convierte tier a precio en centavos."""
        return StripeClient.PRICES.get((tier, interval), 0)
