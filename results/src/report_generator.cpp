#include "powsy365/results/report_generator.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <hpdf.h>

namespace powsys365 {

// ============================================================================
// String helpers
// ============================================================================
std::string reportFormatToString(ReportFormat format) {
    switch (format) {
        case ReportFormat::PDF: return "PDF";
        case ReportFormat::EXCEL: return "Excel";
        case ReportFormat::HTML: return "HTML";
        default: return "Unknown";
    }
}

// ============================================================================
// ReportGenerator
// ============================================================================
ReportGenerator::ReportGenerator() = default;

ReportGenerator::~ReportGenerator() {
    if (m_pdfInitialized) {
        closePdf();
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void ReportGenerator::setConfig(const ReportConfig& config) {
    m_config = config;
}

ReportConfig ReportGenerator::getConfig() const {
    return m_config;
}

void ReportGenerator::setBranding(const CompanyBranding& branding) {
    m_config.branding = branding;
}

// ---------------------------------------------------------------------------
// Report building
// ---------------------------------------------------------------------------
void ReportGenerator::clearSections() {
    m_sections.clear();
}

void ReportGenerator::addSection(const ReportSection& section) {
    m_sections.push_back(section);
}

void ReportGenerator::addExecutiveSummary(const std::string& summary) {
    ReportSection section;
    section.type = ReportSectionType::EXECUTIVE_SUMMARY;
    section.title = "Executive Summary";
    section.content = summary;
    m_sections.push_back(section);
}

void ReportGenerator::addTabularData(const TabularData& data) {
    ReportSection section;
    section.type = ReportSectionType::TABULAR_DATA;
    section.title = data.title;
    section.tableHeaders = data.columnHeaders;
    section.tableRows = data.rows;
    m_sections.push_back(section);
}

void ReportGenerator::addChart(const ChartData& chartData) {
    ReportSection section;
    section.type = ReportSectionType::CHART_IMAGE;
    section.title = chartData.title;
    section.content = chartData.xAxisLabel + " vs " + chartData.yAxisLabel;
    m_sections.push_back(section);
}

void ReportGenerator::addConclusions(const std::vector<std::string>& conclusions) {
    ReportSection section;
    section.type = ReportSectionType::CONCLUSIONS;
    section.title = "Conclusions and Recommendations";
    for (const auto& c : conclusions) {
        section.content += "- " + c + "\n";
    }
    m_sections.push_back(section);
}

void ReportGenerator::addMethodology(const std::string& description) {
    ReportSection section;
    section.type = ReportSectionType::METHODOLOGY;
    section.title = "Methodology";
    section.content = description;
    m_sections.push_back(section);
}

void ReportGenerator::addCustomText(const std::string& title, const std::string& content) {
    ReportSection section;
    section.type = ReportSectionType::CUSTOM_TEXT;
    section.title = title;
    section.content = content;
    m_sections.push_back(section);
}

void ReportGenerator::addPageBreak() {
    ReportSection section;
    section.type = ReportSectionType::PAGE_BREAK;
    m_sections.push_back(section);
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------
bool ReportGenerator::generate() {
    switch (m_config.format) {
        case ReportFormat::PDF:
            return generatePdf();
        case ReportFormat::EXCEL:
            return generateExcel();
        case ReportFormat::HTML:
            return generateHtml();
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// PDF Generation
// ---------------------------------------------------------------------------
bool ReportGenerator::generatePdf() {
    if (!initPdf()) return false;

    HPDF_Page page = HPDF_AddPage(m_pdfDoc);
    m_currentPdfPage = 1;

    // Set page size
    HPDF_Page_SetWidth(page, 595);  // A4
    HPDF_Page_SetHeight(page, 842);

    // Set fonts
    HPDF_Font titleFont = HPDF_GetFont(m_pdfDoc, "Helvetica-Bold", nullptr);
    HPDF_Font headerFont = HPDF_GetFont(m_pdfDoc, "Helvetica-Bold", nullptr);
    HPDF_Font bodyFont = HPDF_GetFont(m_pdfDoc, "Helvetica", nullptr);

    float yPos = 800;
    float leftMargin = 50;
    float pageWidth = 495;

    // Title page
    HPDF_Page_SetFontAndSize(page, titleFont, 24);
    HPDF_Page_SetRGBFill(page,
        m_config.branding.primaryColor.empty() ? 0.12f : 0.12f,
        m_config.branding.primaryColor.empty() ? 0.23f : 0.23f,
        m_config.branding.primaryColor.empty() ? 0.37f : 0.37f);

    // Company name
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, leftMargin, yPos, m_config.branding.companyName.c_str());
    HPDF_Page_EndText(page);

    yPos -= 60;

    // Report title
    HPDF_Page_BeginText(page);
    HPDF_Page_SetFontAndSize(page, titleFont, 18);
    HPDF_Page_TextOut(page, leftMargin, yPos, m_config.branding.reportTitle.c_str());
    HPDF_Page_EndText(page);

    yPos -= 40;

    // Timestamp
    if (m_config.includeTimestamp) {
        auto now = std::chrono::system_clock::now();
        std::time_t nowT = std::chrono::system_clock::to_time_t(now);
        std::tm* nowTm = std::localtime(&nowT);
        char timeStr[128];
        strftime(timeStr, sizeof(timeStr), m_config.dateFormat.c_str(), nowTm);

        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, bodyFont, 10);
        HPDF_Page_TextOut(page, leftMargin, yPos, timeStr);
        HPDF_Page_EndText(page);
    }

    yPos -= 30;

    // Line separator
    HPDF_Page_SetLineWidth(page, 1.0f);
    HPDF_Page_MoveTo(page, leftMargin, yPos);
    HPDF_Page_LineTo(page, leftMargin + pageWidth, yPos);
    HPDF_Page_Stroke(page);

    yPos -= 50;

    // Process sections
    for (const auto& section : m_sections) {
        // Check page break
        if (section.type == ReportSectionType::PAGE_BREAK || yPos < 80) {
            page = HPDF_AddPage(m_pdfDoc);
            m_currentPdfPage++;
            HPDF_Page_SetWidth(page, 595);
            HPDF_Page_SetHeight(page, 842);
            yPos = 800;

            // Header on new page
            writePdfHeader(page);
        }

        // Skip page break sections
        if (section.type == ReportSectionType::PAGE_BREAK) continue;

        writePdfSection(m_pdfDoc, page, section, m_currentPdfPage);

        // Estimate remaining space (simplified)
        yPos -= 40;
        if (section.type == ReportSectionType::TABULAR_DATA) {
            yPos -= static_cast<float>(section.tableRows.size() * 15 + 30);
        }
        if (section.type == ReportSectionType::CUSTOM_TEXT) {
            yPos -= 60;
        }
    }

    // Footer on last page
    writePdfFooter(page, m_currentPdfPage);

    // Save
    HPDF_SaveToFile(m_pdfDoc, m_config.outputFilePath.c_str());
    closePdf();
    return true;
}

void ReportGenerator::writePdfHeader(HPDF_Page page) {
    HPDF_Font font = HPDF_GetFont(m_pdfDoc, "Helvetica", nullptr);
    HPDF_Page_SetFontAndSize(page, font, 8);
    HPDF_Page_SetRGBFill(page, 0.5f, 0.5f, 0.5f);

    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, 810, m_config.headerText.c_str());
    HPDF_Page_EndText(page);

    // Line
    HPDF_Page_SetLineWidth(page, 0.5f);
    HPDF_Page_MoveTo(page, 50, 805);
    HPDF_Page_LineTo(page, 545, 805);
    HPDF_Page_Stroke(page);
}

void ReportGenerator::writePdfFooter(HPDF_Page page, int pageNum) {
    HPDF_Font font = HPDF_GetFont(m_pdfDoc, "Helvetica", nullptr);
    HPDF_Page_SetFontAndSize(page, font, 8);
    HPDF_Page_SetRGBFill(page, 0.5f, 0.5f, 0.5f);

    // Line
    HPDF_Page_SetLineWidth(page, 0.5f);
    HPDF_Page_MoveTo(page, 50, 50);
    HPDF_Page_LineTo(page, 545, 50);
    HPDF_Page_Stroke(page);

    // Footer text
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, 50, 35, m_config.footerText.c_str());
    HPDF_Page_EndText(page);

    // Page number
    if (m_config.includePageNumbers) {
        std::string pageStr = "Page " + std::to_string(pageNum);
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, 500, 35, pageStr.c_str());
        HPDF_Page_EndText(page);
    }
}

void ReportGenerator::writePdfToc(HPDF_Doc) {
    // Table of contents implementation
}

void ReportGenerator::writePdfSection(HPDF_Doc doc, HPDF_Page& page,
                                        const ReportSection& section, int& pageNum) {
    HPDF_Font titleFont = HPDF_GetFont(doc, "Helvetica-Bold", nullptr);
    HPDF_Font bodyFont = HPDF_GetFont(doc, "Helvetica", nullptr);
    float leftMargin = 50;
    float yPos = 750;

    // Section title
    HPDF_Page_SetFontAndSize(page, titleFont, 14);
    HPDF_Page_SetRGBFill(page, 0.12f, 0.23f, 0.37f);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, leftMargin, yPos, section.title.c_str());
    HPDF_Page_EndText(page);

    yPos -= 20;

    switch (section.type) {
        case ReportSectionType::EXECUTIVE_SUMMARY:
        case ReportSectionType::CONCLUSIONS:
        case ReportSectionType::METHODOLOGY:
        case ReportSectionType::CUSTOM_TEXT: {
            HPDF_Page_SetFontAndSize(page, bodyFont, 10);
            HPDF_Page_SetRGBFill(page, 0.2f, 0.2f, 0.2f);

            // Simple text wrapping
            std::string text = section.content;
            size_t pos = 0;
            while (pos < text.length() && yPos > 50) {
                size_t chunkSize = std::min(size_t(90), text.length() - pos);
                std::string chunk = text.substr(pos, chunkSize);

                HPDF_Page_BeginText(page);
                HPDF_Page_TextOut(page, leftMargin, yPos, chunk.c_str());
                HPDF_Page_EndText(page);

                yPos -= 14;
                pos += chunkSize;
            }
            break;
        }
        case ReportSectionType::TABULAR_DATA: {
            if (!section.tableHeaders.empty()) {
                TabularData data;
                data.title = section.title;
                data.columnHeaders = section.tableHeaders;
                data.rows = section.tableRows;
                writePdfTable(page, data, yPos);
            }
            break;
        }
        default:
            break;
    }
}

void ReportGenerator::writePdfTable(HPDF_Page page, const TabularData& data, float& yPos) {
    HPDF_Font headerFont = HPDF_GetFont(m_pdfDoc, "Helvetica-Bold", nullptr);
    HPDF_Font bodyFont = HPDF_GetFont(m_pdfDoc, "Helvetica", nullptr);
    float leftMargin = 50;
    float tableWidth = 495;
    int numCols = static_cast<int>(data.columnHeaders.size());
    float colWidth = tableWidth / numCols;

    // Table background header
    HPDF_Page_SetRGBFill(page, 0.12f, 0.23f, 0.37f);
    HPDF_Page_Rectangle(page, leftMargin, yPos - 15, tableWidth, 18);
    HPDF_Page_Fill(page);

    // Header text
    HPDF_Page_SetFontAndSize(page, headerFont, 9);
    HPDF_Page_SetRGBFill(page, 1.0f, 1.0f, 1.0f);
    float x = leftMargin + 5;
    for (const auto& header : data.columnHeaders) {
        HPDF_Page_BeginText(page);
        HPDF_Page_TextOut(page, x, yPos - 12, header.c_str());
        HPDF_Page_EndText(page);
        x += colWidth;
    }

    yPos -= 20;

    // Rows
    HPDF_Page_SetFontAndSize(page, bodyFont, 8);
    int rowIdx = 0;
    for (const auto& row : data.rows) {
        // Alternate row background
        if (rowIdx % 2 == 0) {
            HPDF_Page_SetRGBFill(page, 0.95f, 0.95f, 0.97f);
        } else {
            HPDF_Page_SetRGBFill(page, 1.0f, 1.0f, 1.0f);
        }
        HPDF_Page_Rectangle(page, leftMargin, yPos - 12, tableWidth, 14);
        HPDF_Page_Fill(page);

        HPDF_Page_SetRGBFill(page, 0.2f, 0.2f, 0.2f);
        x = leftMargin + 5;
        for (const auto& cell : row) {
            HPDF_Page_BeginText(page);
            HPDF_Page_TextOut(page, x, yPos - 10, cell.c_str());
            HPDF_Page_EndText(page);
            x += colWidth;
        }

        yPos -= 14;
        rowIdx++;

        // Check page break
        if (yPos < 80) break;
    }
}

void ReportGenerator::writePdfChart(HPDF_Page, const ChartData&, float&) {
    // Chart rendering would generate image and embed
}

void ReportGenerator::writePdfTextBlock(HPDF_Page, const std::string&, double, double, double, double) {
}

std::string ReportGenerator::generateHtmlChart(const ChartData& chart) {
    std::ostringstream oss;
    oss << "<div style=\"width:100%;height:300px;\">";
    oss << "<canvas id=\"chart_" << chart.title << "\"></canvas>";
    oss << "<script>";
    oss << "new Chart(document.getElementById('chart_" << chart.title << "'),{";
    oss << "type:'" << chart.chartType << "',";
    oss << "data:{";
    if (!chart.categories.empty()) {
        oss << "labels:[";
        for (size_t i = 0; i < chart.categories.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "'" << chart.categories[i] << "'";
        }
        oss << "],";
    }
    oss << "datasets:[";
    for (size_t s = 0; s < chart.seriesNames.size(); ++s) {
        if (s > 0) oss << ",";
        oss << "{label:'" << chart.seriesNames[s] << "',data:[";
        if (s < chart.seriesData.size()) {
            for (size_t i = 0; i < chart.seriesData[s].size(); ++i) {
                if (i > 0) oss << ",";
                oss << chart.seriesData[s][i];
            }
        }
        oss << "]}";
    }
    oss << "]},";
    oss << "options:{responsive:true,maintainAspectRatio:false}});";
    oss << "</script></div>";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Excel Generation (CSV format as placeholder for xlsxwriter)
// ---------------------------------------------------------------------------
bool ReportGenerator::generateExcel() {
    std::ofstream file(m_config.outputFilePath);
    if (!file.is_open()) return false;

    // Write BOM for UTF-8
    file << "\xEF\xBB\xBF";

    // Title
    file << m_config.branding.companyName << "\n";
    file << m_config.branding.reportTitle << "\n";

    // Timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    std::tm* nowTm = std::localtime(&nowT);
    char timeStr[128];
    strftime(timeStr, sizeof(timeStr), m_config.dateFormat.c_str(), nowTm);
    file << "Generated:," << timeStr << "\n\n";

    // Sections
    for (const auto& section : m_sections) {
        file << section.title << "\n";

        switch (section.type) {
            case ReportSectionType::EXECUTIVE_SUMMARY:
            case ReportSectionType::CONCLUSIONS:
            case ReportSectionType::METHODOLOGY:
            case ReportSectionType::CUSTOM_TEXT:
                file << section.content << "\n";
                break;

            case ReportSectionType::TABULAR_DATA: {
                // Headers
                for (size_t i = 0; i < section.tableHeaders.size(); ++i) {
                    if (i > 0) file << ",";
                    file << "\"" << section.tableHeaders[i] << "\"";
                }
                file << "\n";

                // Rows
                for (const auto& row : section.tableRows) {
                    for (size_t i = 0; i < row.size(); ++i) {
                        if (i > 0) file << ",";
                        file << "\"" << row[i] << "\"";
                    }
                    file << "\n";
                }
                break;
            }

            default:
                break;
        }

        file << "\n";
    }

    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// HTML Generation
// ---------------------------------------------------------------------------
bool ReportGenerator::generateHtml() {
    std::ofstream file(m_config.outputFilePath);
    if (!file.is_open()) return false;

    file << "<!DOCTYPE html>\n<html>\n<head>\n";
    file << "<meta charset=\"UTF-8\">\n";
    file << "<title>" << m_config.branding.reportTitle << "</title>\n";
    file << "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>\n";
    file << "<style>\n";
    file << "body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5;color:#333;}\n";
    file << ".container{max-width:1200px;margin:0 auto;background:#fff;padding:30px;box-shadow:0 0 10px rgba(0,0,0,0.1);}\n";
    file << "h1{color:#1E3A5F;border-bottom:3px solid #2E86C1;padding-bottom:10px;}\n";
    file << "h2{color:#1E3A5F;margin-top:30px;border-left:4px solid #2E86C1;padding-left:10px;}\n";
    file << "h3{color:#2E86C1;}\n";
    file << "table{width:100%;border-collapse:collapse;margin:15px 0;}\n";
    file << "th{background:#1E3A5F;color:#fff;padding:10px;text-align:left;}\n";
    file << "td{padding:8px;border-bottom:1px solid #ddd;}\n";
    file << "tr:nth-child(even){background:#f8f9fa;}\n";
    file << "tr:hover{background:#e8f4f8;}\n";
    file << ".highlight{background:#fff3cd!important;}\n";
    file << ".footer{margin-top:40px;padding-top:20px;border-top:1px solid #ddd;text-align:center;color:#666;font-size:0.85em;}\n";
    file << ".timestamp{color:#666;font-style:italic;}\n";
    file << ".summary-box{background:#e8f4f8;border-left:4px solid #2E86C1;padding:15px;margin:15px 0;}\n";
    file << "ul{line-height:1.8;}\n";
    file << ".page-break{page-break-after:always;}\n";
    file << "@media print{.no-print{display:none;}}\n";
    file << "</style>\n</head>\n<body>\n";

    file << "<div class=\"container\">\n";

    // Header
    file << "<div style=\"text-align:center;\">\n";
    file << "<h1 style=\"border:none;text-align:center;\">" << m_config.branding.companyName << "</h1>\n";
    file << "<h2 style=\"border:none;text-align:center;color:#2E86C1;\">" << m_config.branding.reportTitle << "</h2>\n";

    // Timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    std::tm* nowTm = std::localtime(&nowT);
    char timeStr[128];
    strftime(timeStr, sizeof(timeStr), m_config.dateFormat.c_str(), nowTm);
    file << "<p class=\"timestamp\">Generated: " << timeStr << "</p>\n";
    file << "</div>\n";

    // Table of Contents
    if (m_config.includeToc) {
        file << "<h2>Table of Contents</h2>\n<ol>\n";
        int pageNum = 1;
        for (const auto& section : m_sections) {
            if (section.type == ReportSectionType::PAGE_BREAK) {
                pageNum++;
                continue;
            }
            file << "<li><a href=\"#section_" << &section - &m_sections[0] << "\">"
                 << section.title << "</a></li>\n";
        }
        file << "</ol>\n";
    }

    // Sections
    int sectionIdx = 0;
    int pageNum = 1;
    for (const auto& section : m_sections) {
        if (section.type == ReportSectionType::PAGE_BREAK) {
            file << "<div class=\"page-break\"></div>\n";
            pageNum++;
            continue;
        }

        file << "<div id=\"section_" << sectionIdx << "\">\n";
        file << generateHtmlSection(section);
        file << "</div>\n";

        sectionIdx++;
    }

    // Footer
    file << "<div class=\"footer\">\n";
    file << "<p>" << m_config.footerText << "</p>\n";
    if (m_config.includePageNumbers) {
        file << "<p>Page numbers: " << pageNum << " pages</p>\n";
    }
    file << "</div>\n";

    file << "</div>\n</body>\n</html>\n";
    file.close();
    return true;
}

std::string ReportGenerator::generateHtmlSection(const ReportSection& section) {
    std::ostringstream oss;

    switch (section.type) {
        case ReportSectionType::EXECUTIVE_SUMMARY: {
            oss << "<h2>" << section.title << "</h2>\n";
            oss << "<div class=\"summary-box\">\n";
            oss << "<p>" << section.content << "</p>\n";
            oss << "</div>\n";
            break;
        }
        case ReportSectionType::TABULAR_DATA: {
            oss << "<h2>" << section.title << "</h2>\n";
            TabularData data;
            data.title = section.title;
            data.columnHeaders = section.tableHeaders;
            data.rows = section.tableRows;
            oss << generateHtmlTable(data);
            break;
        }
        case ReportSectionType::CHART_IMAGE: {
            oss << "<h2>" << section.title << "</h2>\n";
            oss << "<p><em>Chart: " << section.content << "</em></p>\n";
            break;
        }
        case ReportSectionType::CONCLUSIONS: {
            oss << "<h2>" << section.title << "</h2>\n";
            oss << "<ul>\n";
            std::istringstream iss(section.content);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    // Remove leading "- "
                    if (line.substr(0, 2) == "- ") {
                        line = line.substr(2);
                    }
                    oss << "<li>" << line << "</li>\n";
                }
            }
            oss << "</ul>\n";
            break;
        }
        case ReportSectionType::METHODOLOGY:
        case ReportSectionType::CUSTOM_TEXT: {
            oss << "<h2>" << section.title << "</h2>\n";
            oss << "<p>" << section.content << "</p>\n";
            break;
        }
        default:
            break;
    }

    return oss.str();
}

std::string ReportGenerator::generateHtmlTable(const TabularData& data) {
    std::ostringstream oss;
    oss << "<table>\n<thead>\n<tr>\n";
    for (const auto& header : data.columnHeaders) {
        oss << "<th>" << header << "</th>\n";
    }
    oss << "</tr>\n</thead>\n<tbody>\n";

    for (size_t r = 0; r < data.rows.size(); ++r) {
        bool highlight = std::find(data.highlightRows.begin(), data.highlightRows.end(),
                                     static_cast<int>(r)) != data.highlightRows.end();
        oss << "<tr" << (highlight ? " class=\"highlight\"" : "") << ">\n";
        for (const auto& cell : data.rows[r]) {
            oss << "<td>" << cell << "</td>\n";
        }
        oss << "</tr>\n";
    }

    oss << "</tbody>\n</table>\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
std::string ReportGenerator::getOutputPath() const {
    return m_config.outputFilePath;
}

bool ReportGenerator::saveToFile(const std::string& filePath) {
    m_config.outputFilePath = filePath;
    return generate();
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------
void ReportGenerator::loadTemplate(const std::string&) {
    // Template loading would parse a configuration file
}

// ---------------------------------------------------------------------------
// PDF lifecycle
// ---------------------------------------------------------------------------
bool ReportGenerator::initPdf() {
    m_pdfDoc = HPDF_New(nullptr, nullptr);
    if (!m_pdfDoc) return false;
    m_pdfInitialized = true;
    return true;
}

void ReportGenerator::closePdf() {
    if (m_pdfDoc) {
        HPDF_Free(m_pdfDoc);
        m_pdfDoc = nullptr;
    }
    m_pdfInitialized = false;
}

} // namespace powsys365
