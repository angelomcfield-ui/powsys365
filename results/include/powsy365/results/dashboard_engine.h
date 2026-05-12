#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPainter>
#include <QChart>
#include <QLineSeries>
#include <QValueAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QPieSeries>
#include <QPieSlice>
#include <QTimer>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <functional>

QT_CHARTS_USE_NAMESPACE

namespace powsys365 {

// ---------------------------------------------------------------------------
// Panel types for dashboard
// ---------------------------------------------------------------------------
enum class DashboardPanelType {
    TIME_SERIES,      // Line chart with time on X axis
    GAUGE,            // Circular gauge (voltage, frequency, loading)
    BAR_CHART,        // Vertical/horizontal bar chart
    PIE_CHART,        // Pie/donut chart
    DATA_TABLE,       // Table with values
    STAT_CARD,        // Key metric card
    COLOR_BAR,        // Color bar showing violations
    MAP_VIEW,         // Geographic map overlay
    CUSTOM_WIDGET     // User-defined widget
};

std::string panelTypeToString(DashboardPanelType type);

// ---------------------------------------------------------------------------
// Dashboard layout configuration
// ---------------------------------------------------------------------------
struct PanelLayout {
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    int minWidth = 300;
    int minHeight = 200;
};

// ---------------------------------------------------------------------------
// Time series data point
// ---------------------------------------------------------------------------
struct TimeSeriesPoint {
    std::chrono::system_clock::time_point timestamp;
    double value = 0.0;
};

// ---------------------------------------------------------------------------
// Gauge configuration
// ---------------------------------------------------------------------------
struct GaugeConfig {
    std::string title;
    std::string unit;
    double minValue = 0.0;
    double maxValue = 100.0;
    double warningLow = 0.0;
    double warningHigh = 80.0;
    double alarmLow = 0.0;
    double alarmHigh = 95.0;
    double currentValue = 0.0;
    int numMajorTicks = 10;
    int numMinorTicks = 5;
    bool showDigital = true;
    QColor normalColor = QColor(0, 200, 0);
    QColor warningColor = QColor(255, 200, 0);
    QColor alarmColor = QColor(255, 0, 0);
};

// ---------------------------------------------------------------------------
// Color bar configuration (for violations)
// ---------------------------------------------------------------------------
struct ColorBarConfig {
    std::string title;
    std::vector<std::pair<std::string, double>> items; // name, value
    double minValue = 0.0;
    double maxValue = 100.0;
    double warningThreshold = 80.0;
    double alarmThreshold = 100.0;
    bool horizontal = true;
    std::string unit = "%";
};

// ---------------------------------------------------------------------------
// Stat card configuration
// ---------------------------------------------------------------------------
struct StatCardConfig {
    std::string title;
    std::string value;
    std::string unit;
    std::string subtitle;
    QColor valueColor = QColor(0, 100, 200);
    QColor backgroundColor = QColor(245, 245, 250);
    std::string iconPath;
    double trend = 0.0; // positive = up, negative = down
};

// ---------------------------------------------------------------------------
// Dashboard panel definition
// ---------------------------------------------------------------------------
struct DashboardPanel {
    std::string panelId;
    std::string title;
    DashboardPanelType type;
    PanelLayout layout;
    int refreshIntervalMs = 1000;
    std::function<void()> dataSource;
    std::function<QWidget*()> customWidgetFactory;

    // Panel-specific data
    std::vector<TimeSeriesPoint> timeSeriesData;
    GaugeConfig gaugeConfig;
    ColorBarConfig colorBarConfig;
    StatCardConfig statCardConfig;
    ChartData chartData;
    TabularData tableData;
};

// ---------------------------------------------------------------------------
// Chart data (reused from report_generator)
// ---------------------------------------------------------------------------
struct ChartData {
    std::string chartType;
    std::string title;
    std::string xAxisLabel;
    std::string yAxisLabel;
    std::vector<std::string> categories;
    std::vector<std::string> seriesNames;
    std::vector<std::vector<double>> seriesData;
    double minY = 0.0;
    double maxY = 0.0;
    int width = 800;
    int height = 400;
};

// ---------------------------------------------------------------------------
// Tabular data (reused from report_generator)
// ---------------------------------------------------------------------------
struct TabularData {
    std::string title;
    std::vector<std::string> columnHeaders;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> columnAlignments;
    std::vector<double> columnWidths;
    std::vector<int> highlightRows;
};

// ---------------------------------------------------------------------------
// Dashboard configuration
// ---------------------------------------------------------------------------
struct DashboardConfig {
    std::string dashboardName = "POWSYS365 Dashboard";
    std::string theme = "dark"; // "dark" or "light"
    int columns = 3;             // number of columns in grid
    int refreshIntervalMs = 1000;
    bool autoRefresh = true;
    bool showTitle = true;
    std::string backgroundColor = "#1a1a2e";
    std::string title = "Power System Dashboard";
    std::string timeRange = "1h"; // "15m", "1h", "6h", "1d", "1w"
};

// ---------------------------------------------------------------------------
// Gauge Widget (custom analog gauge)
// ---------------------------------------------------------------------------
class GaugeWidget : public QWidget {
    Q_OBJECT
public:
    explicit GaugeWidget(QWidget* parent = nullptr);
    void setConfig(const GaugeConfig& config);
    void setValue(double value);

protected:
    void paintEvent(QPaintEvent*) override;
    void drawBackground(QPainter* painter);
    void drawArc(QPainter* painter);
    void drawNeedle(QPainter* painter);
    void drawDigitalValue(QPainter* painter);
    void drawTicks(QPainter* painter);

private:
    GaugeConfig m_config;
    double m_currentValue = 0.0;
};

// ---------------------------------------------------------------------------
// Color Bar Widget (for violations)
// ---------------------------------------------------------------------------
class ColorBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorBarWidget(QWidget* parent = nullptr);
    void setConfig(const ColorBarConfig& config);
    void setData(const std::vector<std::pair<std::string, double>>& items);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    ColorBarConfig m_config;
};

// ---------------------------------------------------------------------------
// Stat Card Widget
// ---------------------------------------------------------------------------
class StatCardWidget : public QWidget {
    Q_OBJECT
public:
    explicit StatCardWidget(QWidget* parent = nullptr);
    void setConfig(const StatCardConfig& config);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    StatCardConfig m_config;
};

// ---------------------------------------------------------------------------
// Time Series Chart Widget
// ---------------------------------------------------------------------------
class TimeSeriesChart : public QWidget {
    Q_OBJECT
public:
    explicit TimeSeriesChart(QWidget* parent = nullptr);
    void setTitle(const std::string& title);
    void addSeries(const std::string& name, const QColor& color);
    void addPoint(const std::string& seriesName, const std::chrono::system_clock::time_point& time, double value);
    void clearData();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::string m_title;
    std::map<std::string, std::vector<std::pair<double, double>>> m_series; // time_sec, value
    std::map<std::string, QColor> m_seriesColors;
    double m_minTime = 0;
    double m_maxTime = 1;
    double m_minValue = 0;
    double m_maxValue = 1;
    std::chrono::system_clock::time_point m_startTime;
};

// ---------------------------------------------------------------------------
// Dashboard Engine
// ---------------------------------------------------------------------------
class DashboardEngine : public QWidget {
    Q_OBJECT
public:
    explicit DashboardEngine(QWidget* parent = nullptr);
    ~DashboardEngine() override;

    // Configuration
    void setConfig(const DashboardConfig& config);
    DashboardConfig getConfig() const;

    // Panel management
    void addPanel(const DashboardPanel& panel);
    void removePanel(const std::string& panelId);
    void updatePanelData(const std::string& panelId, const std::vector<TimeSeriesPoint>& data);
    void updatePanelGauge(const std::string& panelId, double value);
    void updatePanelColorBar(const std::string& panelId, const std::vector<std::pair<std::string, double>>& items);
    void updatePanelStatCard(const std::string& panelId, const std::string& value, double trend);
    std::vector<DashboardPanel> getPanels() const;

    // Data feeds
    void addTimeSeriesFeed(const std::string& panelId,
                            std::function<std::vector<TimeSeriesPoint>()> feed);
    void addGaugeFeed(const std::string& panelId, std::function<double()> feed);

    // Layout
    void setLayoutColumns(int columns);
    void arrangePanels();
    void clearDashboard();

    // Refresh
    void startRefresh();
    void stopRefresh();
    void refreshNow();

    // Export
    bool exportToImage(const std::string& filePath);
    void exportAllPanels(std::vector<std::string>& outputPaths);

    // Preset layouts
    void loadPresetLayout(const std::string& layoutName); // "default", "transmission", "generation", "distribution"

public slots:
    void onRefreshTimer();

private:
    void createPanelWidget(const DashboardPanel& panel);
    QWidget* createTimeSeriesPanel(const DashboardPanel& panel);
    QWidget* createGaugePanel(const DashboardPanel& panel);
    QWidget* createBarChartPanel(const DashboardPanel& panel);
    QWidget* createPieChartPanel(const DashboardPanel& panel);
    QWidget* createTablePanel(const DashboardPanel& panel);
    QWidget* createStatCardPanel(const DashboardPanel& panel);
    QWidget* createColorBarPanel(const DashboardPanel& panel);

    DashboardConfig m_config;
    std::vector<DashboardPanel> m_panels;
    std::map<std::string, QWidget*> m_panelWidgets;
    std::map<std::string, std::function<std::vector<TimeSeriesPoint>()>> m_timeSeriesFeeds;
    std::map<std::string, std::function<double()>> m_gaugeFeeds;

    QTimer* m_refreshTimer = nullptr;
    QWidget* m_gridWidget = nullptr;
};

} // namespace powsys365
