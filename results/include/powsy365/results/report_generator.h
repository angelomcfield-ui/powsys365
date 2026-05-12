#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <chrono>

// Forward declarations for external libraries
struct _HPDF_Doc_Rec;
struct _HPDF_Page_Rec;
typedef struct _HPDF_Doc_Rec* HPDF_Doc;
typedef struct _HPDF_Page_Rec* HPDF_Page;

namespace powsys365 {

// ---------------------------------------------------------------------------
// Report format enumeration
// ---------------------------------------------------------------------------
enum class ReportFormat {
    PDF,
    EXCEL,
    HTML
};

std::string reportFormatToString(ReportFormat format);

// ---------------------------------------------------------------------------
// Report section types
// ---------------------------------------------------------------------------
enum class ReportSectionType {
    EXECUTIVE_SUMMARY,
    TABULAR_DATA,
    CHART_IMAGE,
    CONCLUSIONS,
    METHODOLOGY,
    SYSTEM_DESCRIPTION,
    EQUIPMENT_LIST,
    VIOLATIONS_SUMMARY,
    SENSITIVITY_ANALYSIS,
    CUSTOM_TEXT,
    PAGE_BREAK
};

// ---------------------------------------------------------------------------
// Report section
// ---------------------------------------------------------------------------
struct ReportSection {
    ReportSectionType type;
    std::string title;
    std::string content;        // For text sections
    std::vector<std::string> tableHeaders;   // For tabular sections
    std::vector<std::vector<std::string>> tableRows;
    std::string imagePath;      // For chart/image sections
    std::string chartData;      // JSON chart data for HTML
    int pageNumber = 0;
};

// ---------------------------------------------------------------------------
// Company branding configuration
// ---------------------------------------------------------------------------
struct CompanyBranding {
    std::string companyName = "POWSYS365";
    std::string logoPath;
    std::string address;
    std::string phone;
    std::string email;
    std::string website;
    std::string reportTitle = "Power Systems Analysis Report";
    std::string primaryColor = "#1E3A5F";
    std::string secondaryColor = "#2E86C1";
    std::string fontFamily = "Helvetica";
};

// ---------------------------------------------------------------------------
// Report configuration
// ---------------------------------------------------------------------------
struct ReportConfig {
    ReportFormat format = ReportFormat::PDF;
    CompanyBranding branding;
    std::string outputFilePath = "report.pdf";
    bool includeToc = true;
    bool includePageNumbers = true;
    bool includeTimestamp = true;
    bool includeLogo = true;
    std::string headerText;
    std::string footerText = "Confidential - POWSYS365";
    int tableRowsPerPage = 40;
    std::string dateFormat = "%Y-%m-%d %H:%M:%S";
};

// ---------------------------------------------------------------------------
// Tabular data source for reports
// ---------------------------------------------------------------------------
struct TabularData {
    std::string title;
    std::vector<std::string> columnHeaders;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> columnAlignments; // "L", "C", "R" for each column
    std::vector<double> columnWidths;          // relative widths
    std::vector<int> highlightRows;            // row indices to highlight
};

// ---------------------------------------------------------------------------
// Chart data for embedding in reports
// ---------------------------------------------------------------------------
struct ChartData {
    std::string chartType;      // "line", "bar", "pie", "scatter"
    std::string title;
    std::string xAxisLabel;
    std::string yAxisLabel;
    std::vector<std::string> categories;       // X-axis labels
    std::vector<std::string> seriesNames;
    std::vector<std::vector<double>> seriesData;
    double minY = 0.0;
    double maxY = 0.0;
    int width = 800;
    int height = 400;
};

// ---------------------------------------------------------------------------
// Report Generator
// ---------------------------------------------------------------------------
class ReportGenerator {
public:
    ReportGenerator();
    ~ReportGenerator();

    // Configuration
    void setConfig(const ReportConfig& config);
    ReportConfig getConfig() const;
    void setBranding(const CompanyBranding& branding);

    // Report building
    void clearSections();
    void addSection(const ReportSection& section);
    void addExecutiveSummary(const std::string& summary);
    void addTabularData(const TabularData& data);
    void addChart(const ChartData& chartData);
    void addConclusions(const std::vector<std::string>& conclusions);
    void addMethodology(const std::string& description);
    void addCustomText(const std::string& title, const std::string& content);
    void addPageBreak();

    // Generation
    bool generate();
    bool generatePdf();
    bool generateExcel();
    bool generateHtml();

    // Output
    std::string getOutputPath() const;
    bool saveToFile(const std::string& filePath);

    // Templates
    void loadTemplate(const std::string& templatePath);

private:
    // PDF helpers
    bool initPdf();
    void closePdf();
    void writePdfHeader(HPDF_Page page);
    void writePdfFooter(HPDF_Page page, int pageNum);
    void writePdfToc(HPDF_Doc doc);
    void writePdfSection(HPDF_Doc doc, HPDF_Page& page, const ReportSection& section, int& pageNum);
    void writePdfTable(HPDF_Page page, const TabularData& data, double& yPos);
    void writePdfChart(HPDF_Page page, const ChartData& chart, double& yPos);
    void writePdfTextBlock(HPDF_Page page, const std::string& text, double x, double y,
                            double width, double fontSize);
    std::string generateHtmlChart(const ChartData& chart);

    // Excel helpers
    bool writeExcelWorkbook(const std::string& filePath);

    // HTML helpers
    bool writeHtmlDocument(const std::string& filePath);
    std::string generateHtmlHeader();
    std::string generateHtmlFooter();
    std::string generateHtmlToc();
    std::string generateHtmlSection(const ReportSection& section);
    std::string generateHtmlTable(const TabularData& data);

    ReportConfig m_config;
    std::vector<ReportSection> m_sections;

    // PDF state
    HPDF_Doc m_pdfDoc = nullptr;
    int m_currentPdfPage = 0;
    bool m_pdfInitialized = false;
};

} // namespace powsys365
