#pragma once

#include <QObject>
#include <QTimer>
#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QVector2D>
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include <functional>
#include <atomic>

namespace powsys365 {

// Forward declarations
class ScadaHmi;

// ---------------------------------------------------------------------------
// Animation types
// ---------------------------------------------------------------------------
enum class AnimationType {
    FLOW_PARTICLE,       // Moving particles along lines
    COLOR_TRANSITION,    // Smooth color changes
    PULSE,               // Pulsing glow effect
    BLINK,               // Blinking for alarms
    ROTATION,            // Rotating elements (generators)
    SCALE,               // Scale changes
    OPACITY              // Fade in/out
};

// ---------------------------------------------------------------------------
// Easing functions
// ---------------------------------------------------------------------------
enum class EasingType {
    LINEAR,
    EASE_IN_QUAD,
    EASE_OUT_QUAD,
    EASE_IN_OUT_QUAD,
    EASE_IN_CUBIC,
    EASE_OUT_CUBIC,
    EASE_IN_OUT_CUBIC,
    EASE_OUT_BOUNCE,
    EASE_OUT_ELASTIC
};

// ---------------------------------------------------------------------------
// Animation state for an element
// ---------------------------------------------------------------------------
struct AnimationState {
    std::string elementId;
    AnimationType type;
    double progress = 0.0;          // 0.0 to 1.0
    double durationMs = 1000.0;     // total duration
    double currentMs = 0.0;         // current elapsed time
    bool loop = false;
    bool reverseOnComplete = false;
    EasingType easing = EasingType::LINEAR;

    // Start/end values
    QColor startColor = Qt::green;
    QColor endColor = Qt::red;
    double startValue = 0.0;
    double endValue = 1.0;
    double startOpacity = 1.0;
    double endOpacity = 0.0;
    double startScale = 1.0;
    double endScale = 1.5;

    // Current computed values
    QColor currentColor;
    double currentValue = 0.0;
    double currentOpacity = 1.0;
    double currentScale = 1.0;
    QPointF currentPosition;
    double currentRotation = 0.0;

    // Flow-specific
    QPointF flowStart;
    QPointF flowEnd;
    double flowSpeed = 1.0;
    std::vector<QPointF> particlePositions;

    // Callbacks
    std::function<void()> onComplete;
    std::function<void()> onReverse;
};

// ---------------------------------------------------------------------------
// Flow particle configuration
// ---------------------------------------------------------------------------
struct FlowParticleConfig {
    std::string branchId;
    QPointF startPoint;
    QPointF endPoint;
    QColor color = Qt::yellow;
    double particleSize = 4.0;
    int particleCount = 8;
    double speedFactor = 1.0;       // 1.0 = normal, negative = reverse
    double spacing = 0.15;          // spacing between particles (0-1)
    bool showTrail = true;
    double trailLength = 0.3;
    QColor trailColor = QColor(255, 255, 0, 80);
};

// ---------------------------------------------------------------------------
// Status color configuration
// ---------------------------------------------------------------------------
struct StatusColorConfig {
    double normalMin = 0.90;        // Normal range (0.9 to 1.1 for voltage)
    double normalMax = 1.10;
    double warningMin = 0.85;
    double warningMax = 1.15;
    QColor normalColor = QColor(0, 200, 0);
    QColor warningColor = QColor(255, 200, 0);
    QColor alarmColor = QColor(255, 0, 0);
    QColor unknownColor = QColor(150, 150, 150);
    QColor offlineColor = QColor(80, 80, 80);
    double transitionSmoothing = 0.3; // smoothing factor (0-1)
};

// ---------------------------------------------------------------------------
// Animation Engine
// ---------------------------------------------------------------------------
class AnimationEngine : public QObject {
    Q_OBJECT

public:
    explicit AnimationEngine(QObject* parent = nullptr);
    ~AnimationEngine() override;

    // Lifecycle
    void start();
    void stop();
    bool isRunning() const;

    // Frame rate
    void setTargetFrameRate(double fps);
    double getTargetFrameRate() const;
    double getActualFrameRate() const;

    // Flow animations for power lines
    void createFlowAnimation(const FlowParticleConfig& config);
    void removeFlowAnimation(const std::string& branchId);
    void updateFlowSpeed(const std::string& branchId, double speedFactor);
    void setFlowDirection(const std::string& branchId, bool reverse);
    void setFlowVisibility(const std::string& branchId, bool visible);
    std::vector<std::string> getActiveFlowAnimations() const;

    // Color transitions
    void animateColorTransition(const std::string& elementId,
                                  const QColor& fromColor,
                                  const QColor& toColor,
                                  double durationMs = 500.0,
                                  EasingType easing = EasingType::EASE_IN_OUT_QUAD);
    void setInstantColor(const std::string& elementId, const QColor& color);

    // Pulse animations (for alarms)
    void startPulseAnimation(const std::string& elementId,
                             const QColor& pulseColor,
                             double durationMs = 800.0);
    void stopPulseAnimation(const std::string& elementId);

    // Blink animations
    void startBlinkAnimation(const std::string& elementId,
                               double onTimeMs = 500.0,
                               double offTimeMs = 500.0);
    void stopBlinkAnimation(const std::string& elementId);

    // Rotation (for generators)
    void startRotationAnimation(const std::string& elementId,
                                  double rpm,
                                  bool clockwise = true);
    void stopRotationAnimation(const std::string& elementId);
    void setRotationSpeed(const std::string& elementId, double rpm);

    // Scale animations
    void animateScale(const std::string& elementId,
                        double fromScale, double toScale,
                        double durationMs = 300.0,
                        EasingType easing = EasingType::EASE_OUT_QUAD);

    // Opacity/fade
    void animateOpacity(const std::string& elementId,
                          double fromOpacity, double toOpacity,
                          double durationMs = 300.0);

    // Status-based coloring
    void registerStatusColorRule(const std::string& elementId,
                                   const StatusColorConfig& config);
    void updateStatusValue(const std::string& elementId, double normalizedValue);
    QColor getStatusColor(double normalizedValue, const StatusColorConfig& config) const;

    // Electrical-specific helpers
    QColor getVoltageColor(double voltagePu) const;
    QColor getLoadingColor(double loadingPercent) const;
    QColor getFrequencyColor(double frequencyHz) const;
    QColor getBreakerColor(bool closed, bool faulted = false) const;

    // Global config
    void setStatusColorConfig(const StatusColorConfig& config);
    StatusColorConfig getStatusColorConfig() const;

    // Animation state queries
    AnimationState* getAnimationState(const std::string& elementId);
    std::vector<std::string> getActiveAnimations() const;

    // Sync with real-time data
    void syncWithDataSource(std::function<void()> dataRefreshCallback);

    // Render helper - can be called from paint events
    void renderFlowParticles(QPainter* painter, const AnimationState& state);
    void renderPulseEffect(QPainter* painter, const AnimationState& state, const QRectF& bounds);
    void renderGlowEffect(QPainter* painter, const QColor& color, const QRectF& bounds,
                            double intensity);

public slots:
    void onTick();

signals:
    void frameReady();
    void animationCompleted(const QString& elementId);
    void colorChanged(const QString& elementId, const QColor& color);

private:
    void advanceAnimations(double deltaMs);
    void updateFlowParticles(AnimationState& state, double deltaMs);
    double applyEasing(double t, EasingType easing) const;
    QColor interpolateColor(const QColor& from, const QColor& to, double t) const;

    // Timer
    QTimer* m_timer = nullptr;
    std::chrono::steady_clock::time_point m_lastTick;
    double m_targetFps = 30.0;
    std::atomic<double> m_actualFps{0.0};

    // Animation states
    mutable std::mutex m_animationsMutex;
    std::map<std::string, AnimationState> m_animations;

    // Flow particles
    std::map<std::string, FlowParticleConfig> m_flowConfigs;

    // Status color rules
    std::map<std::string, StatusColorConfig> m_statusColorRules;
    StatusColorConfig m_globalStatusConfig;

    // Data sync
    std::function<void()> m_dataRefreshCallback;

    // Running state
    std::atomic<bool> m_running{false};
};

} // namespace powsys365
