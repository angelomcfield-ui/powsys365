#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
POWSYS365 - Unified Payment API
API unificada que abstrae PayPal y Stripe detras de una interfaz comun.

Precios:
    Basic:      $299/mes  o  $2,990/ano
    Pro:        $799/mes  o  $7,990/ano
    Enterprise: $2,499/mes o $24,990/ano
    Lifetime:   $4,999 (unico pago)

Usage:
    api = UnifiedPaymentAPI()
    api.configure_paypal(client_id="...", client_secret="...")
    api.configure_stripe(secret_key="...")
    
    result = api.create_checkout(
        provider=PaymentProvider.STRIPE,
        tier=SubscriptionTier.PRO,
        period=BillingPeriod.YEARLY,
        customer_email="user@example.com"
    )
    print(result.checkout_url)
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Callable, Dict, List, Optional

from paypal_client import PayPalClient, PayPalConfig, PayPalEnvironment
from stripe_client import StripeClient, StripeConfig, SubscriptionTier as StripeTier

logger = logging.getLogger("powsys365.payments")


class PaymentProvider(Enum):
    """Proveedores de pago soportados."""
    PAYPAL = "paypal"
    STRIPE = "stripe"


class SubscriptionTier(Enum):
    """Niveles de suscripcion."""
    BASIC = "basic"
    PRO = "pro"
    ENTERPRISE = "enterprise"
    LIFETIME = "lifetime"


class BillingPeriod(Enum):
    """Periodos de facturacion."""
    MONTHLY = "month"
    YEARLY = "year"
    LIFETIME = "lifetime"


class PaymentStatus(Enum):
    """Estado del pago."""
    PENDING = "pending"
    COMPLETED = "completed"
    FAILED = "failed"
    CANCELLED = "cancelled"
    REFUNDED = "refunded"


# Precios en dolares
PRICE_TABLE: Dict[tuple, float] = {
    (SubscriptionTier.BASIC, BillingPeriod.MONTHLY): 299.0,
    (SubscriptionTier.BASIC, BillingPeriod.YEARLY): 2990.0,
    (SubscriptionTier.PRO, BillingPeriod.MONTHLY): 799.0,
    (SubscriptionTier.PRO, BillingPeriod.YEARLY): 7990.0,
    (SubscriptionTier.ENTERPRISE, BillingPeriod.MONTHLY): 2499.0,
    (SubscriptionTier.ENTERPRISE, BillingPeriod.YEARLY): 24990.0,
    (SubscriptionTier.LIFETIME, BillingPeriod.LIFETIME): 4999.0,
}

# Tier -> max buses mapping
TIER_LIMITS: Dict[SubscriptionTier, dict] = {
    SubscriptionTier.BASIC: {"max_buses": 500, "max_projects": 5},
    SubscriptionTier.PRO: {"max_buses": 5000, "max_projects": 25},
    SubscriptionTier.ENTERPRISE: {"max_buses": 50000, "max_projects": 999},
    SubscriptionTier.LIFETIME: {"max_buses": 999999, "max_projects": 999},
}


def get_price(tier: SubscriptionTier, period: BillingPeriod) -> float:
    """Obtiene el precio para un tier y periodo."""
    return PRICE_TABLE.get((tier, period), 0.0)


def format_price(amount: float, currency: str = "USD") -> str:
    """Formatea un precio."""
    return f"${amount:,.2f} {currency}"


@dataclass
class CheckoutResult:
    """Resultado de crear un checkout."""
    success: bool
    checkout_url: str = ""
    session_id: str = ""
    subscription_id: str = ""
    license_key: str = ""
    amount: float = 0.0
    currency: str = "USD"
    tier: str = ""
    period: str = ""
    error_message: str = ""
    raw_response: Dict[str, Any] = field(default_factory=dict)

    @property
    def is_success(self) -> bool:
        return self.success and bool(self.checkout_url)


@dataclass
class WebhookResult:
    """Resultado de procesar un webhook."""
    verified: bool
    event_type: str = ""
    event_id: str = ""
    provider: str = ""
    subscription_id: str = ""
    customer_email: str = ""
    amount: float = 0.0
    status: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)
    error_message: str = ""


class UnifiedPaymentAPI:
    """
    API unificada de pagos para POWSYS365.
    
    Proporciona una interfaz comun sobre PayPal y Stripe,
    manejando la complejidad de cada proveedor internamente.
    """

    def __init__(self):
        self._paypal: Optional[PayPalClient] = None
        self._stripe: Optional[StripeClient] = None
        self._paypal_configured = False
        self._stripe_configured = False
        self._webhook_handlers: Dict[str, List[Callable]] = {}

    # ------------------------------------------------------------------
    # Configuration
    # ------------------------------------------------------------------
    def configure_paypal(
        self,
        client_id: str,
        client_secret: str,
        sandbox: bool = True,
        webhook_id: str = ""
    ) -> None:
        """
        Configura el cliente PayPal.
        
        Args:
            client_id: Client ID de PayPal
            client_secret: Client Secret de PayPal
            sandbox: True para sandbox, False para produccion
            webhook_id: ID del webhook para verificacion
        """
        env = PayPalEnvironment.SANDBOX if sandbox else PayPalEnvironment.LIVE
        config = PayPalConfig(
            client_id=client_id,
            client_secret=client_secret,
            environment=env,
            webhook_id=webhook_id
        )
        self._paypal = PayPalClient(config)
        self._paypal_configured = True
        logger.info("PayPal configured (%s)", env.value)

    def configure_stripe(
        self,
        secret_key: str,
        publishable_key: str = "",
        webhook_secret: str = ""
    ) -> None:
        """
        Configura el cliente Stripe.
        
        Args:
            secret_key: Secret key de Stripe (sk_...)
            publishable_key: Publishable key (pk_...)
            webhook_secret: Webhook endpoint secret (whsec_...)
        """
        config = StripeConfig(
            secret_key=secret_key,
            publishable_key=publishable_key,
            webhook_secret=webhook_secret
        )
        self._stripe = StripeClient(config)
        self._stripe_configured = True
        logger.info("Stripe configured")

    @property
    def paypal_available(self) -> bool:
        return self._paypal_configured and self._paypal is not None

    @property
    def stripe_available(self) -> bool:
        return self._stripe_configured and self._stripe is not None

    # ------------------------------------------------------------------
    # Checkout Creation
    # ------------------------------------------------------------------
    def create_checkout(
        self,
        provider: PaymentProvider,
        tier: SubscriptionTier,
        period: BillingPeriod,
        customer_email: str,
        customer_name: str = "",
        success_url: str = "",
        cancel_url: str = "",
        metadata: Optional[Dict[str, str]] = None,
    ) -> CheckoutResult:
        """
        Crea una sesion de checkout unificada.
        
        Esta es la funcion principal para iniciar el proceso de pago.
        
        Args:
            provider: PAYPAL o STRIPE
            tier: Nivel de suscripcion
            period: Periodo de facturacion
            customer_email: Email del comprador
            customer_name: Nombre del comprador
            success_url: URL de redireccion exitosa
            cancel_url: URL de redireccion cancelada
            metadata: Metadatos adicionales
        
        Returns:
            CheckoutResult con la URL de checkout o error
        """
        amount = get_price(tier, period)
        if amount <= 0:
            return CheckoutResult(
                success=False,
                error_message=f"Invalid price for {tier.value}/{period.value}"
            )

        license_key = self._generate_license_key(tier)
        meta = metadata or {}
        meta.update({
            "license_key": license_key,
            "tier": tier.value,
            "period": period.value,
            "product": "POWSYS365"
        })

        if provider == PaymentProvider.PAYPAL:
            return self._create_paypal_checkout(
                tier=tier,
                period=period,
                amount=amount,
                customer_email=customer_email,
                customer_name=customer_name,
                license_key=license_key,
                success_url=success_url,
                cancel_url=cancel_url,
                metadata=meta
            )
        elif provider == PaymentProvider.STRIPE:
            return self._create_stripe_checkout(
                tier=tier,
                period=period,
                amount=amount,
                customer_email=customer_email,
                license_key=license_key,
                success_url=success_url,
                cancel_url=cancel_url,
                metadata=meta
            )
        else:
            return CheckoutResult(
                success=False,
                error_message=f"Unknown provider: {provider.value}"
            )

    def _create_paypal_checkout(
        self,
        tier: SubscriptionTier,
        period: BillingPeriod,
        amount: float,
        customer_email: str,
        customer_name: str,
        license_key: str,
        success_url: str,
        cancel_url: str,
        metadata: Dict[str, str]
    ) -> CheckoutResult:
        """Crea checkout via PayPal."""
        if not self.paypal_available:
            return CheckoutResult(
                success=False,
                error_message="PayPal not configured"
            )

        try:
            # Determine mode
            if period == BillingPeriod.LIFETIME:
                # Use one-time payment for lifetime
                result = self._paypal.create_order(
                    amount=amount,
                    currency="USD",
                    description=f"POWSYS365 {tier.value.upper()} - Lifetime",
                    return_url=success_url,
                    cancel_url=cancel_url,
                    custom_id=license_key
                )

                # Extract approval URL
                approval_url = ""
                for link in result.get("links", []):
                    if link.get("rel") == "approve":
                        approval_url = link.get("href", "")
                        break

                return CheckoutResult(
                    success=True,
                    checkout_url=approval_url,
                    session_id=result.get("id", ""),
                    license_key=license_key,
                    amount=amount,
                    tier=tier.value,
                    period=period.value,
                    raw_response=result
                )
            else:
                # Subscription flow
                interval_unit = "YEAR" if period == BillingPeriod.YEARLY else "MONTH"

                # Create product
                product = self._paypal.create_product(
                    name=f"POWSYS365 {tier.value.upper()}",
                    description=f"POWSYS365 Power Systems Analysis - {tier.value.upper()} Tier"
                )

                # Create plan
                plan = self._paypal.create_subscription_plan(
                    product_id=product["id"],
                    name=f"POWSYS365 {tier.value.upper()} Plan",
                    description=f"{tier.value.upper()} subscription for POWSYS365",
                    price=amount,
                    currency="USD",
                    interval_unit=interval_unit,
                    interval_count=1
                )

                # Create subscription
                sub = self._paypal.create_subscription(
                    plan_id=plan["id"],
                    subscriber_email=customer_email,
                    subscriber_name=customer_name,
                    return_url=success_url,
                    cancel_url=cancel_url,
                    custom_id=license_key
                )

                # Extract approval URL
                approval_url = ""
                for link in sub.get("links", []):
                    if link.get("rel") == "approve":
                        approval_url = link.get("href", "")
                        break

                return CheckoutResult(
                    success=True,
                    checkout_url=approval_url,
                    subscription_id=sub.get("id", ""),
                    session_id=sub.get("id", ""),
                    license_key=license_key,
                    amount=amount,
                    tier=tier.value,
                    period=period.value,
                    raw_response=sub
                )

        except Exception as e:
            logger.error("PayPal checkout creation failed: %s", e)
            return CheckoutResult(
                success=False,
                error_message=str(e),
                license_key=license_key
            )

    def _create_stripe_checkout(
        self,
        tier: SubscriptionTier,
        period: BillingPeriod,
        amount: float,
        customer_email: str,
        license_key: str,
        success_url: str,
        cancel_url: str,
        metadata: Dict[str, str]
    ) -> CheckoutResult:
        """Crea checkout via Stripe."""
        if not self.stripe_available:
            return CheckoutResult(
                success=False,
                error_message="Stripe not configured"
            )

        try:
            # Build price data
            amount_cents = int(amount * 100)
            currency = "usd"
            interval = "year" if period == BillingPeriod.YEARLY else "month"

            if period == BillingPeriod.LIFETIME:
                # One-time payment
                result = self._stripe.create_checkout_session(
                    customer_email=customer_email,
                    price_data={
                        "unit_amount": amount_cents,
                        "currency": currency,
                        "product_data": {
                            "name": f"POWSYS365 {tier.value.upper()} - Lifetime",
                            "description": "Lifetime license for POWSYS365"
                        },
                    },
                    mode="payment",
                    success_url=success_url,
                    cancel_url=cancel_url,
                    metadata=metadata,
                    client_reference_id=license_key,
                )
            else:
                # Subscription
                result = self._stripe.create_checkout_session(
                    customer_email=customer_email,
                    price_data={
                        "unit_amount": amount_cents,
                        "currency": currency,
                        "product_data": {
                            "name": f"POWSYS365 {tier.value.upper()}",
                            "description": f"{tier.value.upper()} subscription - {interval}ly"
                        },
                        "recurring": {
                            "interval": interval,
                            "interval_count": 1
                        }
                    },
                    mode="subscription",
                    success_url=success_url,
                    cancel_url=cancel_url,
                    metadata=metadata,
                    client_reference_id=license_key,
                )

            return CheckoutResult(
                success=True,
                checkout_url=result.get("url", ""),
                session_id=result.get("id", ""),
                subscription_id=result.get("subscription", ""),
                license_key=license_key,
                amount=amount,
                tier=tier.value,
                period=period.value,
                raw_response=result
            )

        except Exception as e:
            logger.error("Stripe checkout creation failed: %s", e)
            return CheckoutResult(
                success=False,
                error_message=str(e),
                license_key=license_key
            )

    # ------------------------------------------------------------------
    # Subscription Management
    # ------------------------------------------------------------------
    def cancel_subscription(
        self,
        provider: PaymentProvider,
        subscription_id: str,
        immediately: bool = False
    ) -> bool:
        """
        Cancela una suscripcion activa.
        
        Args:
            provider: PAYPAL o STRIPE
            subscription_id: ID de la suscripcion
            immediately: True=cancela ahora, False=al final del periodo
        """
        if provider == PaymentProvider.PAYPAL and self.paypal_available:
            return self._paypal.cancel_subscription(subscription_id)
        elif provider == PaymentProvider.STRIPE and self.stripe_available:
            try:
                self._stripe.cancel_subscription(subscription_id, immediately)
                return True
            except Exception as e:
                logger.error("Stripe cancel failed: %s", e)
                return False
        return False

    def get_subscription_status(
        self,
        provider: PaymentProvider,
        subscription_id: str
    ) -> str:
        """Obtiene el estado de una suscripcion."""
        if provider == PaymentProvider.PAYPAL and self.paypal_available:
            try:
                result = self._paypal.get_subscription(subscription_id)
                return result.get("status", "unknown")
            except Exception as e:
                logger.error("PayPal status check failed: %s", e)
                return "error"
        elif provider == PaymentProvider.STRIPE and self.stripe_available:
            return self._stripe.get_subscription_status(subscription_id)
        return "unknown"

    # ------------------------------------------------------------------
    # Webhook Handling
    # ------------------------------------------------------------------
    def handle_webhook(
        self,
        provider: PaymentProvider,
        headers: Dict[str, str],
        body: bytes,
    ) -> WebhookResult:
        """
        Procesa y verifica un webhook entrante.
        
        Args:
            provider: PAYPAL o STRIPE
            headers: Headers del request HTTP
            body: Cuerpo raw del request (bytes)
        
        Returns:
            WebhookResult con el evento verificado
        """
        if provider == PaymentProvider.PAYPAL:
            return self._handle_paypal_webhook(headers, body)
        elif provider == PaymentProvider.STRIPE:
            return self._handle_stripe_webhook(headers, body)
        else:
            return WebhookResult(verified=False, error_message="Unknown provider")

    def _handle_paypal_webhook(
        self,
        headers: Dict[str, str],
        body: bytes
    ) -> WebhookResult:
        """Procesa webhook de PayPal."""
        if not self.paypal_available:
            return WebhookResult(verified=False, error_message="PayPal not configured")

        transmission_id = headers.get("PAYPAL-TRANSMISSION-ID", "")
        cert_url = headers.get("PAYPAL-CERT-URL", "")
        auth_algo = headers.get("PAYPAL-AUTH-ALGO", "")
        transmission_sig = headers.get("PAYPAL-TRANSMISSION-SIG", "")

        try:
            verified = self._paypal.verify_webhook(
                transmission_id=transmission_id,
                cert_url=cert_url,
                auth_algo=auth_algo,
                transmission_sig=transmission_sig,
                webhook_body=body.decode("utf-8")
            )

            event_data = {}
            try:
                event_data = json.loads(body)
            except Exception:
                pass

            if verified:
                result = WebhookResult(
                    verified=True,
                    event_type=event_data.get("event_type", ""),
                    event_id=event_data.get("id", ""),
                    provider="paypal",
                    subscription_id=self._extract_subscription_id(event_data),
                    status=self._extract_status(event_data),
                    metadata=event_data
                )
                self._notify_handlers(result)
                return result
            else:
                return WebhookResult(
                    verified=False,
                    error_message="Signature verification failed",
                    metadata=event_data
                )

        except Exception as e:
            return WebhookResult(verified=False, error_message=str(e))

    def _handle_stripe_webhook(
        self,
        headers: Dict[str, str],
        body: bytes
    ) -> WebhookResult:
        """Procesa webhook de Stripe."""
        if not self.stripe_available:
            return WebhookResult(verified=False, error_message="Stripe not configured")

        sig_header = headers.get("Stripe-Signature", "")

        try:
            event = self._stripe.construct_webhook_event(
                payload=body,
                sig_header=sig_header
            )

            result = WebhookResult(
                verified=True,
                event_type=event.get("type", ""),
                event_id=event.get("id", ""),
                provider="stripe",
                subscription_id=self._extract_subscription_id(event),
                status=self._extract_status(event),
                metadata=event
            )
            self._notify_handlers(result)
            return result

        except Exception as e:
            return WebhookResult(verified=False, error_message=str(e))

    def _extract_subscription_id(self, event_data: Dict) -> str:
        """Extrae subscription_id de un evento."""
        resource = event_data.get("resource", {})
        if not resource:
            resource = event_data.get("data", {}).get("object", {})
        return resource.get("id", "")

    def _extract_status(self, event_data: Dict) -> str:
        """Extrae el estado de un evento."""
        resource = event_data.get("resource", {})
        if not resource:
            resource = event_data.get("data", {}).get("object", {})
        return resource.get("status", "")

    def register_webhook_handler(
        self,
        event_type: str,
        handler: Callable[[WebhookResult], None]
    ) -> None:
        """
        Registra un handler para un tipo de evento de webhook.
        
        Args:
            event_type: Tipo de evento (ej: 'checkout.session.completed')
            handler: Funcion callback
        """
        if event_type not in self._webhook_handlers:
            self._webhook_handlers[event_type] = []
        self._webhook_handlers[event_type].append(handler)
        logger.info("Webhook handler registered for: %s", event_type)

    def _notify_handlers(self, result: WebhookResult) -> None:
        """Notifica handlers registrados."""
        handlers = self._webhook_handlers.get(result.event_type, [])
        for handler in handlers:
            try:
                handler(result)
            except Exception as e:
                logger.error("Webhook handler error: %s", e)

    # ------------------------------------------------------------------
    # Utility
    # ------------------------------------------------------------------
    @staticmethod
    def _generate_license_key(tier: SubscriptionTier) -> str:
        """Genera una license key en formato XXXX-XXXX-XXXX-XXXX."""
        import random
        import string

        prefix_map = {
            SubscriptionTier.BASIC: "BS",
            SubscriptionTier.PRO: "PR",
            SubscriptionTier.ENTERPRISE: "EN",
            SubscriptionTier.LIFETIME: "LT"
        }
        prefix = prefix_map.get(tier, "TR")

        chars = string.ascii_uppercase + string.digits
        
        def make_group(length: int = 4) -> str:
            return "".join(random.choices(chars, k=length))

        g1 = prefix + make_group(2)
        return f"{g1}-{make_group()}-{make_group()}-{make_group()}"

    def health_check(self) -> Dict[str, Any]:
        """Verifica el estado de ambos proveedores."""
        result = {
            "paypal": {"configured": self.paypal_available},
            "stripe": {"configured": self.stripe_available},
        }

        if self.paypal_available:
            result["paypal"].update(self._paypal.health_check())

        if self.stripe_available:
            result["stripe"].update(self._stripe.health_check())

        result["overall"] = "healthy" if (
            self.paypal_available or self.stripe_available
        ) else "no_providers_configured"

        return result

    def get_price_list(self) -> List[Dict[str, Any]]:
        """Devuelve lista de precios para mostrar en UI."""
        prices = []
        for tier in SubscriptionTier:
            for period in [BillingPeriod.MONTHLY, BillingPeriod.YEARLY]:
                if tier == SubscriptionTier.LIFETIME and period != BillingPeriod.YEARLY:
                    continue
                
                price = get_price(tier, period)
                limits = TIER_LIMITS.get(tier, {})
                
                prices.append({
                    "tier": tier.value,
                    "tier_display": tier.value.upper(),
                    "period": period.value,
                    "period_display": "Monthly" if period == BillingPeriod.MONTHLY else "Yearly",
                    "price": price,
                    "price_display": format_price(price),
                    "max_buses": limits.get("max_buses", 0),
                    "max_projects": limits.get("max_projects", 0),
                })
            
            # Lifetime
            if tier == SubscriptionTier.LIFETIME:
                price = get_price(tier, BillingPeriod.LIFETIME)
                limits = TIER_LIMITS.get(tier, {})
                prices.append({
                    "tier": tier.value,
                    "tier_display": "LIFETIME",
                    "period": "lifetime",
                    "period_display": "One-Time",
                    "price": price,
                    "price_display": format_price(price),
                    "max_buses": limits.get("max_buses", 0),
                    "max_projects": limits.get("max_projects", 0),
                })

        return prices


# JSON helpers
import json


def to_json(obj: Any) -> str:
    """Serializa a JSON."""
    return json.dumps(obj, indent=2, default=str)


if __name__ == "__main__":
    # Demo usage
    logging.basicConfig(level=logging.INFO)
    
    api = UnifiedPaymentAPI()
    
    # Print price list
    print("=== POWSYS365 Pricing ===")
    for item in api.get_price_list():
        print(f"  {item['tier_display']:12} {item['period_display']:10} {item['price_display']:15} "
              f"(buses: {item['max_buses']:,}, projects: {item['max_projects']})")
