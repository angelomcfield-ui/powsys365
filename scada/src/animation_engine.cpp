#include "powsy365/scada/animation_engine.h"
#include <QPainter>
#include <cmath>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// AnimationEngine
// ============================================================================
AnimationEngine::AnimationEngine(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &AnimationEngine::onTick);
    m_lastTick = std::chrono::steady_clock::now();
}

AnimationEngine::~AnimationEngine() {
    stop();
}

void AnimationEngine::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_lastTick = std::chrono::steady_clock::now();
    int intervalMs = static_cast<int>(1000.0 / m_targetFps);
    m_timer->start(intervalMs);
}

void AnimationEngine::stop() {
    m_running.store(false);
    m_timer->stop();
}

bool AnimationEngine::isRunning() const {
    return m_running.load();
}

// ---------------------------------------------------------------------------
// Frame rate
// ---------------------------------------------------------------------------
void AnimationEngine::setTargetFrameRate(double fps) {
    m_targetFps = std::clamp(fps, 1.0, 120.0);
    if (m_running.load()) {
        int intervalMs = static_cast<int>(1000.0 / m_targetFps);
        m_timer->setInterval(intervalMs);
    }
}

double AnimationEngine::getTargetFrameRate() const {
    return m_targetFps;
}

double AnimationEngine::getActualFrameRate() const {
    return m_actualFps.load();
}

// ---------------------------------------------------------------------------
// Flow animations for power lines
// ---------------------------------------------------------------------------
void AnimationEngine::createFlowAnimation(const FlowParticleConfig& config) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    AnimationState state;
    state.elementId = config.branchId;
    state.type = AnimationType::FLOW_PARTICLE;
    state.flowStart = config.startPoint;
    state.flowEnd = config.endPoint;
    state.flowSpeed = config.speedFactor;
    state.durationMs = 2000.0;
    state.loop = true;
    state.currentColor = config.color;
    state.currentOpacity = 1.0;

    m_animations[config.branchId] = state;
    m_flowConfigs[config.branchId] = config;
}

void AnimationEngine::removeFlowAnimation(const std::string& branchId) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    m_animations.erase(branchId);
    m_flowConfigs.erase(branchId);
}

void AnimationEngine::updateFlowSpeed(const std::string& branchId, double speedFactor) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(branchId);
    if (it != m_animations.end()) {
        it->second.flowSpeed = speedFactor;
    }
    auto cfgIt = m_flowConfigs.find(branchId);
    if (cfgIt != m_flowConfigs.end()) {
        cfgIt->second.speedFactor = speedFactor;
    }
}

void AnimationEngine::setFlowDirection(const std::string& branchId, bool reverse) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto cfgIt = m_flowConfigs.find(branchId);
    if (cfgIt != m_flowConfigs.end()) {
        cfgIt->second.speedFactor = reverse ?
            -std::abs(cfgIt->second.speedFactor) :
             std::abs(cfgIt->second.speedFactor);
    }
    auto it = m_animations.find(branchId);
    if (it != m_animations.end()) {
        it->second.flowSpeed = reverse ?
            -std::abs(it->second.flowSpeed) : std::abs(it->second.flowSpeed);
    }
}

void AnimationEngine::setFlowVisibility(const std::string& branchId, bool visible) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(branchId);
    if (it != m_animations.end()) {
        it->second.currentOpacity = visible ? 1.0 : 0.0;
    }
}

std::vector<std::string> AnimationEngine::getActiveFlowAnimations() const {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    std::vector<std::string> result;
    for (const auto& [id, state] : m_animations) {
        if (state.type == AnimationType::FLOW_PARTICLE) {
            result.push_back(id);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Color transitions
// ---------------------------------------------------------------------------
void AnimationEngine::animateColorTransition(const std::string& elementId,
                                               const QColor& fromColor,
                                               const QColor& toColor,
                                               double durationMs,
                                               EasingType easing) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    AnimationState state;
    state.elementId = elementId;
    state.type = AnimationType::COLOR_TRANSITION;
    state.startColor = fromColor;
    state.endColor = toColor;
    state.currentColor = fromColor;
    state.durationMs = durationMs;
    state.easing = easing;
    state.progress = 0.0;
    state.currentMs = 0.0;

    auto it = m_animations.find(elementId);
    if (it != m_animations.end()) {
        // Preserve some properties from existing animation
        state.currentPosition = it->second.currentPosition;
        state.currentScale = it->second.currentScale;
        state.currentOpacity = it->second.currentOpacity;
    }

    m_animations[elementId] = state;
}

void AnimationEngine::setInstantColor(const std::string& elementId, const QColor& color) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(elementId);
    if (it != m_animations.end()) {
        it->second.currentColor = color;
        it->second.startColor = color;
        it->second.endColor = color;
    } else {
        AnimationState state;
        state.elementId = elementId;
        state.type = AnimationType::COLOR_TRANSITION;
        state.startColor = color;
        state.endColor = color;
        state.currentColor = color;
        state.durationMs = 0;
        state.progress = 1.0;
        m_animations[elementId] = state;
    }
}

// ---------------------------------------------------------------------------
// Pulse animations (for alarms)
// ---------------------------------------------------------------------------
void AnimationEngine::startPulseAnimation(const std::string& elementId,
                                            const QColor& pulseColor,
                                            double durationMs) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    AnimationState state;
    state.elementId = elementId;
    state.type = AnimationType::PULSE;
    state.startColor = pulseColor;
    state.endColor = pulseColor;
    state.currentColor = pulseColor;
    state.durationMs = durationMs;
    state.loop = true;
    state.easing = EasingType::EASE_IN_OUT_QUAD;
    state.progress = 0.0;
    state.currentMs = 0.0;
    state.currentOpacity = 1.0;

    m_animations[elementId] = state;
}

void AnimationEngine::stopPulseAnimation(const std::string& elementId) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(elementId);
    if (it != m_animations.end() && it->second.type == AnimationType::PULSE) {
        m_animations.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Blink animations
// ---------------------------------------------------------------------------
void AnimationEngine::startBlinkAnimation(const std::string& elementId,
                                            double onTimeMs,
                                            double offTimeMs) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    AnimationState state;
    state.elementId = elementId;
    state.type = AnimationType::BLINK;
    state.durationMs = onTimeMs + offTimeMs;
    state.loop = true;
    state.progress = 0.0;
    state.currentMs = 0.0;
    state.currentOpacity = 1.0;

    m_animations[elementId] = state;
}

void AnimationEngine::stopBlinkAnimation(const std::string& elementId) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(elementId);
    if (it != m_animations.end() && it->second.type == AnimationType::BLINK) {
        m_animations.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Rotation (for generators)
// ---------------------------------------------------------------------------
void AnimationEngine::startRotationAnimation(const std::string& elementId,
                                               double rpm,
                                               bool clockwise) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    AnimationState state;
    state.elementId = elementId;
    state.type = AnimationType::ROTATION;
    state.currentValue = clockwise ? rpm : -rpm;
    state.durationMs = 0; // Continuous
    state.loop = true;
    state.progress = 0.0;

    m_animations[elementId] = state;
}

void AnimationEngine::stopRotationAnimation(const std::string& elementId) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(elementId);
    if (it != m_animations.end() && it->second.type == AnimationType::ROTATION) {
        m_animations.erase(it);
    }
}

void AnimationEngine::setRotationSpeed(const std::string& elementId, double rpm) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(elementId);
    if (it != m_animations.end()) {
        it->second.currentValue = rpm;
    }
}

// ---------------------------------------------------------------------------
// Scale animations
// ---------------------------------------------------------------------------
void AnimationEngine::animateScale(const std::string& elementId,
                                     double fromScale, double toScale,
                                     double durationMs,
                                     EasingType easing) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    AnimationState state;
    state.elementId = elementId;
    state.type = AnimationType::SCALE;
    state.startScale = fromScale;
    state.endScale = toScale;
    state.currentScale = fromScale;
    state.durationMs = durationMs;
    state.easing = easing;
    state.progress = 0.0;
    state.currentMs = 0.0;

    m_animations[elementId] = state;
}

// ---------------------------------------------------------------------------
// Opacity/fade
// ---------------------------------------------------------------------------
void AnimationEngine::animateOpacity(const std::string& elementId,
                                       double fromOpacity, double toOpacity,
                                       double durationMs) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    AnimationState state;
    state.elementId = elementId;
    state.type = AnimationType::OPACITY;
    state.startOpacity = fromOpacity;
    state.endOpacity = toOpacity;
    state.currentOpacity = fromOpacity;
    state.durationMs = durationMs;
    state.easing = EasingType::EASE_IN_OUT_QUAD;
    state.progress = 0.0;
    state.currentMs = 0.0;

    m_animations[elementId] = state;
}

// ---------------------------------------------------------------------------
// Status-based coloring
// ---------------------------------------------------------------------------
void AnimationEngine::registerStatusColorRule(const std::string& elementId,
                                                const StatusColorConfig& config) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    m_statusColorRules[elementId] = config;
}

void AnimationEngine::updateStatusValue(const std::string& elementId, double normalizedValue) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_statusColorRules.find(elementId);
    if (it == m_statusColorRules.end()) return;

    QColor targetColor = getStatusColor(normalizedValue, it->second);
    auto animIt = m_animations.find(elementId);
    if (animIt != m_animations.end()) {
        // Transition smoothly
        QColor current = animIt->second.currentColor;
        double smoothing = it->second.transitionSmoothing;
        int r = static_cast<int>(current.red() * (1.0 - smoothing) + targetColor.red() * smoothing);
        int g = static_cast<int>(current.green() * (1.0 - smoothing) + targetColor.green() * smoothing);
        int b = static_cast<int>(current.blue() * (1.0 - smoothing) + targetColor.blue() * smoothing);
        animIt->second.currentColor = QColor(
            std::clamp(r, 0, 255),
            std::clamp(g, 0, 255),
            std::clamp(b, 0, 255)
        );
    } else {
        AnimationState state;
        state.elementId = elementId;
        state.type = AnimationType::COLOR_TRANSITION;
        state.currentColor = targetColor;
        state.startColor = targetColor;
        state.endColor = targetColor;
        state.durationMs = 0;
        state.progress = 1.0;
        m_animations[elementId] = state;
    }
}

QColor AnimationEngine::getStatusColor(double normalizedValue, const StatusColorConfig& config) const {
    if (normalizedValue >= config.normalMin && normalizedValue <= config.normalMax) {
        return config.normalColor;
    }
    if (normalizedValue >= config.warningMin && normalizedValue <= config.warningMax) {
        // Interpolate between normal and warning
        if (normalizedValue < config.normalMin) {
            double t = (config.normalMin - normalizedValue) / (config.normalMin - config.warningMin);
            t = std::clamp(t, 0.0, 1.0);
            return interpolateColor(config.normalColor, config.warningColor, t);
        } else {
            double t = (normalizedValue - config.normalMax) / (config.warningMax - config.normalMax);
            t = std::clamp(t, 0.0, 1.0);
            return interpolateColor(config.normalColor, config.warningColor, t);
        }
    }
    // Alarm zone
    if (normalizedValue < config.warningMin) {
        double t = (config.warningMin - normalizedValue) / config.warningMin;
        t = std::clamp(t, 0.0, 1.0);
        return interpolateColor(config.warningColor, config.alarmColor, t);
    } else {
        double t = (normalizedValue - config.warningMax) / (2.0 - config.warningMax);
        t = std::clamp(t, 0.0, 1.0);
        return interpolateColor(config.warningColor, config.alarmColor, t);
    }
}

QColor AnimationEngine::getVoltageColor(double voltagePu) const {
    return getStatusColor(voltagePu, m_globalStatusConfig);
}

QColor AnimationEngine::getLoadingColor(double loadingPercent) const {
    if (loadingPercent < 80.0) return QColor(0, 200, 0);
    if (loadingPercent < 90.0) {
        double t = (loadingPercent - 80.0) / 10.0;
        return interpolateColor(QColor(0, 200, 0), QColor(255, 200, 0), t);
    }
    if (loadingPercent < 100.0) {
        double t = (loadingPercent - 90.0) / 10.0;
        return interpolateColor(QColor(255, 200, 0), QColor(255, 150, 0), t);
    }
    if (loadingPercent < 110.0) {
        double t = (loadingPercent - 100.0) / 10.0;
        return interpolateColor(QColor(255, 150, 0), QColor(255, 0, 0), t);
    }
    return QColor(255, 0, 0);
}

QColor AnimationEngine::getFrequencyColor(double frequencyHz) const {
    double pu = frequencyHz / 60.0;
    if (pu >= 0.998 && pu <= 1.002) return QColor(0, 200, 0);
    if (pu >= 0.995 && pu <= 1.005) return QColor(255, 200, 0);
    return QColor(255, 0, 0);
}

QColor AnimationEngine::getBreakerColor(bool closed, bool faulted) const {
    if (faulted) return QColor(255, 0, 0);
    if (closed) return QColor(0, 200, 0);
    return QColor(150, 150, 150);
}

// ---------------------------------------------------------------------------
// Global config
// ---------------------------------------------------------------------------
void AnimationEngine::setStatusColorConfig(const StatusColorConfig& config) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    m_globalStatusConfig = config;
}

StatusColorConfig AnimationEngine::getStatusColorConfig() const {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    return m_globalStatusConfig;
}

// ---------------------------------------------------------------------------
// Animation state queries
// ---------------------------------------------------------------------------
AnimationState* AnimationEngine::getAnimationState(const std::string& elementId) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    auto it = m_animations.find(elementId);
    if (it != m_animations.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> AnimationEngine::getActiveAnimations() const {
    std::lock_guard<std::mutex> lock(m_animationsMutex);
    std::vector<std::string> result;
    for (const auto& [id, state] : m_animations) {
        (void)state;
        result.push_back(id);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Sync with real-time data
// ---------------------------------------------------------------------------
void AnimationEngine::syncWithDataSource(std::function<void()> dataRefreshCallback) {
    m_dataRefreshCallback = dataRefreshCallback;
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------
void AnimationEngine::renderFlowParticles(QPainter* painter, const AnimationState& state) {
    auto cfgIt = m_flowConfigs.find(state.elementId);
    if (cfgIt == m_flowConfigs.end()) return;

    const auto& config = cfgIt->second;
    QPointF direction = state.flowEnd - state.flowStart;
    double length = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
    if (length < 0.001) return;

    // Generate particle positions based on progress
    for (int i = 0; i < config.particleCount; ++i) {
        double baseProgress = (static_cast<double>(i) / config.particleCount) + state.progress;
        baseProgress = std::fmod(baseProgress, 1.0);

        QPointF pos = state.flowStart + direction * baseProgress;

        if (config.showTrail) {
            QPointF trailStart = pos - direction * config.trailLength;
            QLinearGradient gradient(trailStart, pos);
            QColor trailC = config.trailColor;
            gradient.setColorAt(0.0, QColor(trailC.red(), trailC.green(), trailC.blue(), 0));
            gradient.setColorAt(1.0, trailC);
            painter->setPen(QPen(gradient, config.particleSize, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(trailStart, pos);
        }

        painter->setBrush(QBrush(config.color));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(pos, config.particleSize / 2.0, config.particleSize / 2.0);
    }
}

void AnimationEngine::renderPulseEffect(QPainter* painter, const AnimationState& state, const QRectF& bounds) {
    double pulseIntensity = std::sin(state.progress * M_PI) * 0.5 + 0.5;
    QColor glowColor = state.currentColor;
    glowColor.setAlphaF(pulseIntensity * 0.6);

    double margin = 5.0 * pulseIntensity;
    QRectF glowBounds = bounds.adjusted(-margin, -margin, margin, margin);

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(glowColor, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter->drawRect(glowBounds);
}

void AnimationEngine::renderGlowEffect(QPainter* painter, const QColor& color, const QRectF& bounds,
                                         double intensity) {
    QColor glowColor = color;
    glowColor.setAlphaF(intensity * 0.5);

    double margin = 8.0;
    QRectF glowBounds = bounds.adjusted(-margin, -margin, margin, margin);

    // Multiple rings for glow effect
    for (int i = 3; i >= 0; --i) {
        double ringIntensity = intensity * (1.0 - i * 0.25);
        QColor ringColor = glowColor;
        ringColor.setAlphaF(ringIntensity * 0.3);
        double ringMargin = margin * (1.0 + i * 0.5);
        QRectF ringBounds = bounds.adjusted(-ringMargin, -ringMargin, ringMargin, ringMargin);
        painter->setPen(QPen(ringColor, 2.0 - i * 0.3, Qt::SolidLine, Qt::RoundCap));
        painter->drawRoundedRect(ringBounds, 4, 4);
    }
}

// ---------------------------------------------------------------------------
// Tick handler
// ---------------------------------------------------------------------------
void AnimationEngine::onTick() {
    auto now = std::chrono::steady_clock::now();
    double deltaMs = std::chrono::duration<double, std::milli>(now - m_lastTick).count();
    m_lastTick = now;

    // Calculate actual FPS
    static double fpsAccumulator = 0;
    static int fpsCount = 0;
    fpsAccumulator += deltaMs;
    fpsCount++;
    if (fpsAccumulator > 500.0) {
        m_actualFps.store(fpsCount * 1000.0 / fpsAccumulator);
        fpsAccumulator = 0;
        fpsCount = 0;
    }

    advanceAnimations(deltaMs);

    if (m_dataRefreshCallback) {
        m_dataRefreshCallback();
    }

    emit frameReady();
}

// ---------------------------------------------------------------------------
// Animation advancement
// ---------------------------------------------------------------------------
void AnimationEngine::advanceAnimations(double deltaMs) {
    std::lock_guard<std::mutex> lock(m_animationsMutex);

    for (auto& [id, state] : m_animations) {
        (void)id;

        if (state.durationMs > 0) {
            state.currentMs += deltaMs;
            state.progress = state.currentMs / state.durationMs;

            if (state.progress >= 1.0) {
                if (state.loop) {
                    if (state.reverseOnComplete) {
                        std::swap(state.startColor, state.endColor);
                        std::swap(state.startValue, state.endValue);
                        std::swap(state.startScale, state.endScale);
                        std::swap(state.startOpacity, state.endOpacity);
                    }
                    state.progress = 0.0;
                    state.currentMs = 0.0;
                } else {
                    state.progress = 1.0;
                    if (state.onComplete) {
                        state.onComplete();
                    }
                }
            }
        }

        double t = applyEasing(state.progress, state.easing);

        // Update current values based on animation type
        switch (state.type) {
            case AnimationType::FLOW_PARTICLE: {
                state.progress = std::fmod(state.progress + (state.flowSpeed * deltaMs / state.durationMs), 1.0);
                state.currentMs = state.progress * state.durationMs;
                updateFlowParticles(state, deltaMs);
                break;
            }
            case AnimationType::COLOR_TRANSITION: {
                state.currentColor = interpolateColor(state.startColor, state.endColor, t);
                break;
            }
            case AnimationType::PULSE: {
                double pulseT = applyEasing(state.progress, EasingType::EASE_IN_OUT_QUAD);
                double alpha = std::sin(pulseT * M_PI) * 0.6 + 0.4;
                state.currentColor = state.startColor;
                state.currentColor.setAlphaF(alpha);
                break;
            }
            case AnimationType::BLINK: {
                state.currentOpacity = (state.progress < 0.5) ? 1.0 : 0.2;
                break;
            }
            case AnimationType::ROTATION: {
                double angleDelta = (state.currentValue / 60.0) * 360.0 * (deltaMs / 1000.0);
                state.currentRotation += angleDelta;
                if (state.currentRotation >= 360.0) state.currentRotation -= 360.0;
                break;
            }
            case AnimationType::SCALE: {
                state.currentScale = state.startScale + (state.endScale - state.startScale) * t;
                break;
            }
            case AnimationType::OPACITY: {
                state.currentOpacity = state.startOpacity + (state.endOpacity - state.startOpacity) * t;
                break;
            }
        }
    }
}

void AnimationEngine::updateFlowParticles(AnimationState& state, double deltaMs) {
    (void)deltaMs;
    (void)state;
}

// ---------------------------------------------------------------------------
// Easing functions
// ---------------------------------------------------------------------------
double AnimationEngine::applyEasing(double t, EasingType easing) const {
    switch (easing) {
        case EasingType::LINEAR:
            return t;
        case EasingType::EASE_IN_QUAD:
            return t * t;
        case EasingType::EASE_OUT_QUAD:
            return 1.0 - (1.0 - t) * (1.0 - t);
        case EasingType::EASE_IN_OUT_QUAD: {
            if (t < 0.5) return 2.0 * t * t;
            return 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
        }
        case EasingType::EASE_IN_CUBIC:
            return t * t * t;
        case EasingType::EASE_OUT_CUBIC:
            return 1.0 - std::pow(1.0 - t, 3.0);
        case EasingType::EASE_IN_OUT_CUBIC: {
            if (t < 0.5) return 4.0 * t * t * t;
            return 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
        }
        case EasingType::EASE_OUT_BOUNCE: {
            if (t < 1.0 / 2.75) return 7.5625 * t * t;
            if (t < 2.0 / 2.75) return 7.5625 * (t -= 1.5 / 2.75) * t + 0.75;
            if (t < 2.5 / 2.75) return 7.5625 * (t -= 2.25 / 2.75) * t + 0.9375;
            return 7.5625 * (t -= 2.625 / 2.75) * t + 0.984375;
        }
        case EasingType::EASE_OUT_ELASTIC: {
            if (t == 0) return 0.0;
            if (t == 1) return 1.0;
            double c4 = (2.0 * M_PI) / 3.0;
            return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * c4) + 1.0;
        }
        default:
            return t;
    }
}

QColor AnimationEngine::interpolateColor(const QColor& from, const QColor& to, double t) const {
    t = std::clamp(t, 0.0, 1.0);
    int r = static_cast<int>(from.red() * (1.0 - t) + to.red() * t);
    int g = static_cast<int>(from.green() * (1.0 - t) + to.green() * t);
    int b = static_cast<int>(from.blue() * (1.0 - t) + to.blue() * t);
    int a = static_cast<int>(from.alpha() * (1.0 - t) + to.alpha() * t);
    return QColor(
        std::clamp(r, 0, 255),
        std::clamp(g, 0, 255),
        std::clamp(b, 0, 255),
        std::clamp(a, 0, 255)
    );
}

} // namespace powsys365
