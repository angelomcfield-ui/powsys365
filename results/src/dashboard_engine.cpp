#include "powsy365/results/dashboard_engine.h"
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDateTime>
#include <QFile>
#include <QBuffer>
#include <QPixmap>
#include <cmath>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// String helpers
// ============================================================================
std::string panelTypeToString(DashboardPanelType type) {
    switch (type) {
        case DashboardPanelType::TIME_SERIES: return "Time Series";
        case DashboardPanelType::GAUGE: return "Gauge";
        case DashboardPanelType::BAR_CHART: return "Bar Chart";
        case DashboardPanelType::PIE_CHART: return "Pie Chart";
        case DashboardPanelType::DATA_TABLE: return "Data Table";
        case DashboardPanelType::STAT_CARD: return "Stat Card";
        case DashboardPanelType::COLOR_BAR: return "Color Bar";
        case DashboardPanelType::MAP_VIEW: return "Map View";
        case DashboardPanelType::CUSTOM_WIDGET: return "Custom Widget";
        default: return "Unknown";
    }
}

// ============================================================================
// GaugeWidget
// ============================================================================
GaugeWidget::GaugeWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 200);
}

void GaugeWidget::setConfig(const GaugeConfig& config) {
    m_config = config;
    update();
}

void GaugeWidget::setValue(double value) {
    m_currentValue = value;
    update();
}

void GaugeWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int size = std::min(w, h) - 20;
    QRectF rect((w - size) / 2.0, (h - size) / 2.0 + 10, size, size);

    drawBackground(&painter);
    drawArc(&painter);
    drawTicks(&painter);
    drawNeedle(&painter);
    drawDigitalValue(&painter);
}

void GaugeWidget::drawBackground(QPainter* painter) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(30, 30, 50));
    painter->drawRect(rect());
}

void GaugeWidget::drawArc(QPainter* painter) {
    int w = width();
    int h = height();
    int size = std::min(w, h) - 40;
    QRectF arcRect((w - size) / 2.0, (h - size) / 2.0 - 10, size, size);

    // Background arc
    QPen bgPen(QColor(60, 60, 80), 12, Qt::SolidLine, Qt::FlatCap);
    painter->setPen(bgPen);
    painter->drawArc(arcRect, 225 * 16, -270 * 16);

    // Colored arc based on value
    double valueRatio = (m_currentValue - m_config.minValue) /
                         (m_config.maxValue - m_config.minValue);
    valueRatio = std::clamp(valueRatio, 0.0, 1.0);

    QColor arcColor = m_config.normalColor;
    if (m_currentValue >= m_config.alarmHigh || m_currentValue <= m_config.alarmLow) {
        arcColor = m_config.alarmColor;
    } else if (m_currentValue >= m_config.warningHigh || m_currentValue <= m_config.warningLow) {
        arcColor = m_config.warningColor;
    }

    QPen valuePen(arcColor, 12, Qt::SolidLine, Qt::FlatCap);
    painter->setPen(valuePen);
    painter->drawArc(arcRect, 225 * 16, static_cast<int>(-270 * 16 * valueRatio));
}

void GaugeWidget::drawTicks(QPainter* painter) {
    int w = width();
    int h = height();
    int radius = std::min(w, h) / 2 - 30;
    QPointF center(w / 2.0, h / 2.0 - 10);

    painter->setPen(QPen(QColor(150, 150, 150), 1));
    for (int i = 0; i <= m_config.numMajorTicks; ++i) {
        double angle = 225.0 - (270.0 * i / m_config.numMajorTicks);
        double rad = angle * M_PI / 180.0;
        QPointF outer(center.x() + (radius + 5) * cos(rad),
                       center.y() - (radius + 5) * sin(rad));
        QPointF inner(center.x() + radius * cos(rad),
                       center.y() - radius * sin(rad));
        painter->drawLine(outer, inner);

        // Labels
        double val = m_config.minValue + (m_config.maxValue - m_config.minValue) *
                       i / m_config.numMajorTicks;
        QString label = QString::number(val, 'f', 0);
        QFont font = painter->font();
        font.setPointSize(7);
        painter->setFont(font);
        QPointF textPos(center.x() + (radius - 25) * cos(rad),
                         center.y() - (radius - 25) * sin(rad));
        painter->drawText(textPos, label);
    }
}

void GaugeWidget::drawNeedle(QPainter* painter) {
    int w = width();
    int h = height();
    int radius = std::min(w, h) / 2 - 45;
    QPointF center(w / 2.0, h / 2.0 - 10);

    double valueRatio = (m_currentValue - m_config.minValue) /
                         (m_config.maxValue - m_config.minValue);
    valueRatio = std::clamp(valueRatio, 0.0, 1.0);
    double angle = 225.0 - (270.0 * valueRatio);
    double rad = angle * M_PI / 180.0;

    QPointF tip(center.x() + radius * cos(rad),
                 center.y() - radius * sin(rad));

    QPen needlePen(Qt::white, 2);
    painter->setPen(needlePen);
    painter->drawLine(center, tip);

    // Center dot
    painter->setBrush(Qt::white);
    painter->drawEllipse(center, 5, 5);
}

void GaugeWidget::drawDigitalValue(QPainter* painter) {
    if (!m_config.showDigital) return;

    QFont font = painter->font();
    font.setPointSize(14);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(Qt::white);

    QString text = QString::number(m_currentValue, 'f', 2);
    if (!m_config.unit.empty()) {
        text += " " + QString::fromStdString(m_config.unit);
    }

    QRectF textRect(0, height() - 45, width(), 30);
    painter->drawText(textRect, Qt::AlignCenter, text);

    // Title
    font.setPointSize(9);
    font.setBold(false);
    painter->setFont(font);
    painter->setPen(QColor(180, 180, 180));
    QRectF titleRect(0, 5, width(), 20);
    painter->drawText(titleRect, Qt::AlignCenter,
                       QString::fromStdString(m_config.title));
}

// ============================================================================
// ColorBarWidget
// ============================================================================
ColorBarWidget::ColorBarWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 100);
}

void ColorBarWidget::setConfig(const ColorBarConfig& config) {
    m_config = config;
    update();
}

void ColorBarWidget::setData(const std::vector<std::pair<std::string, double>>& items) {
    m_config.items = items;
    update();
}

void ColorBarWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Background
    painter.setBrush(QColor(30, 30, 50));
    painter.drawRect(rect());

    // Title
    QFont font = painter->font();
    font.setPointSize(10);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter.drawText(10, 18, QString::fromStdString(m_config.title));

    int barHeight = 18;
    int barY = 30;
    int maxBarWidth = width() - 100;
    int labelWidth = 80;

    font.setPointSize(8);
    font.setBold(false);
    painter->setFont(font);

    for (const auto& item : m_config.items) {
        if (barY + barHeight > height() - 5) break;

        // Label
        painter.setPen(QColor(200, 200, 200));
        painter.drawText(10, barY + 13, QString::fromStdString(item.first));

        // Bar
        double ratio = (item.second - m_config.minValue) /
                        (m_config.maxValue - m_config.minValue);
        ratio = std::clamp(ratio, 0.0, 1.0);
        int barWidth = static_cast<int>(maxBarWidth * ratio);

        QColor barColor(0, 200, 0);
        if (item.second >= m_config.alarmThreshold) {
            barColor = QColor(255, 0, 0);
        } else if (item.second >= m_config.warningThreshold) {
            barColor = QColor(255, 200, 0);
        }

        painter->setBrush(barColor);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(labelWidth, barY, std::max(barWidth, 2), barHeight - 2, 3, 3);

        // Value text
        painter->setPen(Qt::white);
        QString valStr = QString::number(item.second, 'f', 1) + " " +
                          QString::fromStdString(m_config.unit);
        painter->drawText(labelWidth + barWidth + 5, barY + 13, valStr);

        barY += barHeight + 4;
    }
}

// ============================================================================
// StatCardWidget
// ============================================================================
StatCardWidget::StatCardWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(150, 100);
}

void StatCardWidget::setConfig(const StatCardConfig& config) {
    m_config = config;
    update();
}

void StatCardWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Background
    painter->setBrush(m_config.backgroundColor);
    painter->setPen(QColor(220, 220, 220));
    painter->drawRoundedRect(rect().adjusted(2, 2, -2, -2), 8, 8);

    // Title
    QFont font = painter->font();
    font.setPointSize(9);
    painter->setFont(font);
    painter->setPen(QColor(120, 120, 120));
    painter->drawText(15, 22, QString::fromStdString(m_config.title));

    // Value
    font.setPointSize(22);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(m_config.valueColor);
    QString valText = QString::fromStdString(m_config.value);
    if (!m_config.unit.empty()) {
        valText += " " + QString::fromStdString(m_config.unit);
    }
    painter->drawText(15, 55, valText);

    // Subtitle
    font.setPointSize(8);
    font.setBold(false);
    painter->setFont(font);
    painter->setPen(QColor(150, 150, 150));
    painter->drawText(15, 75, QString::fromStdString(m_config.subtitle));

    // Trend indicator
    if (m_config.trend != 0.0) {
        QColor trendColor = m_config.trend > 0 ? QColor(0, 180, 0) : QColor(255, 0, 0);
        QString trendStr = (m_config.trend > 0 ? "+" : "") +
                            QString::number(m_config.trend, 'f', 1) + "%";
        painter->setPen(trendColor);
        painter->drawText(width() - 60, 75, trendStr);
    }
}

// ============================================================================
// TimeSeriesChart
// ============================================================================
TimeSeriesChart::TimeSeriesChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(300, 200);
    m_startTime = std::chrono::system_clock::now();
}

void TimeSeriesChart::setTitle(const std::string& title) {
    m_title = title;
    update();
}

void TimeSeriesChart::addSeries(const std::string& name, const QColor& color) {
    m_series[name] = {};
    m_seriesColors[name] = color;
}

void TimeSeriesChart::addPoint(const std::string& seriesName,
                                 const std::chrono::system_clock::time_point& time,
                                 double value) {
    auto it = m_series.find(seriesName);
    if (it == m_series.end()) return;

    double timeSec = std::chrono::duration<double>(time - m_startTime).count();
    it->second.push_back({timeSec, value});

    // Update bounds
    m_minTime = std::min(m_minTime, timeSec);
    m_maxTime = std::max(m_maxTime, timeSec);
    m_minValue = std::min(m_minValue, value);
    m_maxValue = std::max(m_maxValue, value);

    // Limit data points
    if (it->second.size() > 1000) {
        it->second.erase(it->second.begin());
    }

    update();
}

void TimeSeriesChart::clearData() {
    for (auto& [name, data] : m_series) {
        (void)name;
        data.clear();
    }
    m_minTime = 0;
    m_maxTime = 1;
    m_minValue = 0;
    m_maxValue = 1;
    update();
}

void TimeSeriesChart::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Background
    painter.setBrush(QColor(25, 25, 40));
    painter.setPen(Qt::NoPen);
    painter.drawRect(rect());

    int marginLeft = 55;
    int marginRight = 20;
    int marginTop = 30;
    int marginBottom = 35;

    int plotW = width() - marginLeft - marginRight;
    int plotH = height() - marginTop - marginBottom;

    // Title
    QFont font = painter.font();
    font.setPointSize(10);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(marginLeft, 18, QString::fromStdString(m_title));

    // Grid
    painter.setPen(QPen(QColor(50, 50, 70), 1));
    for (int i = 0; i <= 5; ++i) {
        int y = marginTop + (plotH * i) / 5;
        painter.drawLine(marginLeft, y, marginLeft + plotW, y);
    }
    for (int i = 0; i <= 6; ++i) {
        int x = marginLeft + (plotW * i) / 6;
        painter.drawLine(x, marginTop, x, marginTop + plotH);
    }

    // Y-axis labels
    font.setPointSize(7);
    font.setBold(false);
    painter.setFont(font);
    painter.setPen(QColor(150, 150, 150));
    double yRange = m_maxValue - m_minValue;
    if (yRange < 1e-10) yRange = 1.0;
    for (int i = 0; i <= 5; ++i) {
        double val = m_maxValue - yRange * i / 5.0;
        int y = marginTop + (plotH * i) / 5;
        QString label = QString::number(val, 'f', 2);
        painter.drawText(2, y + 4, label);
    }

    // Plot series
    for (const auto& [name, data] : m_series) {
        (void)name;
        if (data.size() < 2) continue;

        QColor color = m_seriesColors[name];
        painter.setPen(QPen(color, 1.5));

        double timeRange = m_maxTime - m_minTime;
        if (timeRange < 1e-10) timeRange = 1.0;

        QPointF prevPoint;
        bool first = true;
        for (const auto& pt : data) {
            double x = marginLeft + (pt.first - m_minTime) / timeRange * plotW;
            double y = marginTop + (1.0 - (pt.second - m_minValue) / yRange) * plotH;

            if (!first) {
                painter.drawLine(prevPoint, QPointF(x, y));
            }
            prevPoint = QPointF(x, y);
            first = false;
        }
    }

    // Border
    painter.setPen(QPen(QColor(60, 60, 80), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(marginLeft, marginTop, plotW, plotH);
}

// ============================================================================
// DashboardEngine
// ============================================================================
DashboardEngine::DashboardEngine(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);

    m_gridWidget = new QWidget(this);
    mainLayout->addWidget(m_gridWidget);

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &DashboardEngine::onRefreshTimer);

    setStyleSheet("background-color: #1a1a2e;");
    setMinimumSize(1024, 768);
}

DashboardEngine::~DashboardEngine() {
    stopRefresh();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void DashboardEngine::setConfig(const DashboardConfig& config) {
    m_config = config;
}

DashboardConfig DashboardEngine::getConfig() const {
    return m_config;
}

// ---------------------------------------------------------------------------
// Panel management
// ---------------------------------------------------------------------------
void DashboardEngine::addPanel(const DashboardPanel& panel) {
    m_panels.push_back(panel);
    createPanelWidget(panel);
    arrangePanels();
}

void DashboardEngine::removePanel(const std::string& panelId) {
    auto it = std::remove_if(m_panels.begin(), m_panels.end(),
        [&panelId](const DashboardPanel& p) { return p.panelId == panelId; });
    m_panels.erase(it, m_panels.end());

    auto wit = m_panelWidgets.find(panelId);
    if (wit != m_panelWidgets.end()) {
        delete wit->second;
        m_panelWidgets.erase(wit);
    }

    arrangePanels();
}

void DashboardEngine::updatePanelData(const std::string& panelId,
                                        const std::vector<TimeSeriesPoint>& data) {
    auto it = m_panelWidgets.find(panelId);
    if (it == m_panelWidgets.end()) return;

    auto* chart = dynamic_cast<TimeSeriesChart*>(it->second);
    if (chart && !data.empty()) {
        for (const auto& pt : data) {
            chart->addPoint("series1", pt.timestamp, pt.value);
        }
    }
}

void DashboardEngine::updatePanelGauge(const std::string& panelId, double value) {
    auto it = m_panelWidgets.find(panelId);
    if (it == m_panelWidgets.end()) return;

    auto* gauge = dynamic_cast<GaugeWidget*>(it->second);
    if (gauge) {
        gauge->setValue(value);
    }
}

void DashboardEngine::updatePanelColorBar(const std::string& panelId,
                                            const std::vector<std::pair<std::string, double>>& items) {
    auto it = m_panelWidgets.find(panelId);
    if (it == m_panelWidgets.end()) return;

    auto* bar = dynamic_cast<ColorBarWidget*>(it->second);
    if (bar) {
        bar->setData(items);
    }
}

void DashboardEngine::updatePanelStatCard(const std::string& panelId,
                                            const std::string& value, double trend) {
    auto it = m_panelWidgets.find(panelId);
    if (it == m_panelWidgets.end()) return;

    auto* card = dynamic_cast<StatCardWidget*>(it->second);
    if (card) {
        StatCardConfig config;
        config.value = value;
        config.trend = trend;
        card->setConfig(config);
    }
}

std::vector<DashboardPanel> DashboardEngine::getPanels() const {
    return m_panels;
}

// ---------------------------------------------------------------------------
// Data feeds
// ---------------------------------------------------------------------------
void DashboardEngine::addTimeSeriesFeed(const std::string& panelId,
                                          std::function<std::vector<TimeSeriesPoint>()> feed) {
    m_timeSeriesFeeds[panelId] = feed;
}

void DashboardEngine::addGaugeFeed(const std::string& panelId, std::function<double()> feed) {
    m_gaugeFeeds[panelId] = feed;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void DashboardEngine::setLayoutColumns(int columns) {
    m_config.columns = columns;
    arrangePanels();
}

void DashboardEngine::arrangePanels() {
    // Delete old layout
    QLayout* oldLayout = m_gridWidget->layout();
    if (oldLayout) {
        while (QLayoutItem* item = oldLayout->takeAt(0)) {
            delete item;
        }
        delete oldLayout;
    }

    auto* gridLayout = new QGridLayout(m_gridWidget);
    gridLayout->setSpacing(8);
    gridLayout->setContentsMargins(8, 8, 8, 8);

    for (const auto& panel : m_panels) {
        auto it = m_panelWidgets.find(panel.panelId);
        if (it == m_panelWidgets.end()) continue;

        QWidget* widget = it->second;
        widget->setMinimumSize(panel.layout.minWidth, panel.layout.minHeight);

        gridLayout->addWidget(widget, panel.layout.row, panel.layout.column,
                               panel.layout.rowSpan, panel.layout.columnSpan);
    }

    m_gridWidget->setLayout(gridLayout);
}

void DashboardEngine::clearDashboard() {
    for (auto& [id, widget] : m_panelWidgets) {
        (void)id;
        delete widget;
    }
    m_panelWidgets.clear();
    m_panels.clear();
    m_timeSeriesFeeds.clear();
    m_gaugeFeeds.clear();

    QLayout* oldLayout = m_gridWidget->layout();
    if (oldLayout) {
        while (QLayoutItem* item = oldLayout->takeAt(0)) {
            delete item;
        }
        delete oldLayout;
    }
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------
void DashboardEngine::startRefresh() {
    if (!m_refreshTimer->isActive()) {
        m_refreshTimer->start(m_config.refreshIntervalMs);
    }
}

void DashboardEngine::stopRefresh() {
    m_refreshTimer->stop();
}

void DashboardEngine::refreshNow() {
    onRefreshTimer();
}

void DashboardEngine::onRefreshTimer() {
    // Update time series feeds
    for (const auto& [panelId, feed] : m_timeSeriesFeeds) {
        auto data = feed();
        updatePanelData(panelId, data);
    }

    // Update gauge feeds
    for (const auto& [panelId, feed] : m_gaugeFeeds) {
        double value = feed();
        updatePanelGauge(panelId, value);
    }
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
bool DashboardEngine::exportToImage(const std::string& filePath) {
    QPixmap pixmap(size());
    render(&pixmap);
    return pixmap.save(QString::fromStdString(filePath));
}

void DashboardEngine::exportAllPanels(std::vector<std::string>& outputPaths) {
    outputPaths.clear();
    int idx = 0;
    for (const auto& [id, widget] : m_panelWidgets) {
        (void)id;
        QPixmap pixmap(widget->size());
        widget->render(&pixmap);
        std::string path = "panel_" + std::to_string(idx++) + ".png";
        if (pixmap.save(QString::fromStdString(path))) {
            outputPaths.push_back(path);
        }
    }
}

// ---------------------------------------------------------------------------
// Preset layouts
// ---------------------------------------------------------------------------
void DashboardEngine::loadPresetLayout(const std::string& layoutName) {
    clearDashboard();

    if (layoutName == "transmission") {
        // Row 0: System overview
        DashboardPanel p1;
        p1.panelId = "voltage_profile";
        p1.title = "Voltage Profile";
        p1.type = DashboardPanelType::TIME_SERIES;
        p1.layout = {0, 0, 1, 2};
        addPanel(p1);

        DashboardPanel p2;
        p2.panelId = "freq_gauge";
        p2.title = "Frequency";
        p2.type = DashboardPanelType::GAUGE;
        p2.layout = {0, 2, 1, 1};
        p2.gaugeConfig = {"Frequency", "Hz", 59.0, 61.0, 59.5, 60.5, 59.8, 60.2, 60.0};
        addPanel(p2);

        // Row 1: Loading and stats
        DashboardPanel p3;
        p3.panelId = "loading_bars";
        p3.title = "Line Loading";
        p3.type = DashboardPanelType::COLOR_BAR;
        p3.layout = {1, 0, 1, 2};
        addPanel(p3);

        DashboardPanel p4;
        p4.panelId = "total_mw";
        p4.title = "Total Generation";
        p4.type = DashboardPanelType::STAT_CARD;
        p4.layout = {1, 2, 1, 1};
        p4.statCardConfig = {"Total Generation", "1250.5", "MW", "Real-time output", QColor(0, 150, 255)};
        addPanel(p4);
    } else if (layoutName == "default") {
        DashboardPanel p1;
        p1.panelId = "main_chart";
        p1.title = "System Overview";
        p1.type = DashboardPanelType::TIME_SERIES;
        p1.layout = {0, 0, 1, 2};
        addPanel(p1);

        DashboardPanel p2;
        p2.panelId = "sys_freq";
        p2.title = "System Frequency";
        p2.type = DashboardPanelType::GAUGE;
        p2.layout = {0, 2, 1, 1};
        p2.gaugeConfig = {"Frequency", "Hz", 59.0, 61.0, 59.5, 60.5, 59.8, 60.2, 60.0};
        addPanel(p2);

        DashboardPanel p3;
        p3.panelId = "loading";
        p3.title = "Loading";
        p3.type = DashboardPanelType::COLOR_BAR;
        p3.layout = {1, 0, 1, 2};
        addPanel(p3);
    }
}

// ---------------------------------------------------------------------------
// Panel widget creation
// ---------------------------------------------------------------------------
void DashboardEngine::createPanelWidget(const DashboardPanel& panel) {
    QWidget* widget = nullptr;

    switch (panel.type) {
        case DashboardPanelType::TIME_SERIES:
            widget = createTimeSeriesPanel(panel);
            break;
        case DashboardPanelType::GAUGE:
            widget = createGaugePanel(panel);
            break;
        case DashboardPanelType::BAR_CHART:
            widget = createBarChartPanel(panel);
            break;
        case DashboardPanelType::PIE_CHART:
            widget = createPieChartPanel(panel);
            break;
        case DashboardPanelType::DATA_TABLE:
            widget = createTablePanel(panel);
            break;
        case DashboardPanelType::STAT_CARD:
            widget = createStatCardPanel(panel);
            break;
        case DashboardPanelType::COLOR_BAR:
            widget = createColorBarPanel(panel);
            break;
        case DashboardPanelType::CUSTOM_WIDGET:
            if (panel.customWidgetFactory) {
                widget = panel.customWidgetFactory();
            }
            break;
        default:
            break;
    }

    if (widget) {
        m_panelWidgets[panel.panelId] = widget;
    }
}

QWidget* DashboardEngine::createTimeSeriesPanel(const DashboardPanel& panel) {
    auto* chart = new TimeSeriesChart(this);
    chart->setTitle(panel.title);
    chart->addSeries("series1", QColor(0, 200, 255));
    chart->addSeries("series2", QColor(255, 200, 0));
    chart->addSeries("series3", QColor(0, 255, 100));
    return chart;
}

QWidget* DashboardEngine::createGaugePanel(const DashboardPanel& panel) {
    auto* gauge = new GaugeWidget(this);
    gauge->setConfig(panel.gaugeConfig);
    return gauge;
}

QWidget* DashboardEngine::createBarChartPanel(const DashboardPanel&) {
    auto* widget = new QWidget(this);
    widget->setMinimumSize(200, 150);
    return widget;
}

QWidget* DashboardEngine::createPieChartPanel(const DashboardPanel&) {
    auto* widget = new QWidget(this);
    widget->setMinimumSize(200, 150);
    return widget;
}

QWidget* DashboardEngine::createTablePanel(const DashboardPanel&) {
    auto* widget = new QWidget(this);
    widget->setMinimumSize(200, 150);
    return widget;
}

QWidget* DashboardEngine::createStatCardPanel(const DashboardPanel& panel) {
    auto* card = new StatCardWidget(this);
    card->setConfig(panel.statCardConfig);
    return card;
}

QWidget* DashboardEngine::createColorBarPanel(const DashboardPanel& panel) {
    auto* bar = new ColorBarWidget(this);
    bar->setConfig(panel.colorBarConfig);
    return bar;
}

} // namespace powsys365
