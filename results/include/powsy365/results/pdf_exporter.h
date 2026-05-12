#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

// Forward declarations for libharu
struct _HPDF_Doc_Rec;
struct _HPDF_Page_Rec;
struct _HPDF_Font_Rec;
typedef struct _HPDF_Doc_Rec* HPDF_Doc;
typedef struct _HPDF_Page_Rec* HPDF_Page;
typedef struct _HPDF_Font_Rec* HPDF_Font;

namespace powsys365 {

// ---------------------------------------------------------------------------
// PDF style configuration
// ---------------------------------------------------------------------------
struct PdfStyle {
    std::string fontName = "Helvetica";
    float titleFontSize = 16.0f;
    float headerFontSize = 12.0f;
    float bodyFontSize = 9.0f;
    float tableHeaderFontSize = 9.0f;
    float tableBodyFontSize = 8.0f;
    float footerFontSize = 8.0f;

    // Colors (RGB 0-1)
    float titleR = 0.12f, titleG = 0.23f, titleB = 0.37f;
    float headerR = 0.18f, headerG = 0.53f, headerB = 0.76f;
    float bodyR = 0.2f, bodyG = 0.2f, bodyB = 0.2f;
    float tableHeaderR = 1.0f, tableHeaderG = 1.0f, tableHeaderB = 1.0f;
    float tableHeaderBgR = 0.12f, tableHeaderBgG = 0.23f, tableHeaderBgB = 0.37f;
    float tableAltBgR = 0.95f, tableAltBgG = 0.95f, tableAltBgB = 0.97f;
    float lineR = 0.7f, lineG = 0.7f, lineB = 0.7f;

    float pageMargin = 30.0f;
    float tableRowHeight = 16.0f;
    float tableHeaderHeight = 20.0f;
    float lineSpacing = 1.2f;
};

// ---------------------------------------------------------------------------
// PDF page configuration
// ---------------------------------------------------------------------------
struct PdfPageConfig {
    float pageWidth = 595.0f;   // A4 width in points
    float pageHeight = 842.0f;  // A4 height in points
    bool landscape = false;
    std::string pageSize = "A4"; // "A4", "Letter", "Legal"
};

// ---------------------------------------------------------------------------
// Table column definition
// ---------------------------------------------------------------------------
struct PdfTableColumn {
    std::string header;
    float width = 0.0f;         // 0 = auto
    std::string alignment = "L"; // "L", "C", "R"
    int precision = 2;          // for numeric columns
};

// ---------------------------------------------------------------------------
// Table definition
// ---------------------------------------------------------------------------
struct PdfTable {
    std::string title;
    std::vector<PdfTableColumn> columns;
    std::vector<std::vector<std::string>> rows;
    std::vector<int> highlightRows; // Row indices to highlight
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
};

// ---------------------------------------------------------------------------
// Header/Footer content
// ---------------------------------------------------------------------------
struct PdfHeaderFooter {
    std::string leftText;
    std::string centerText;
    std::string rightText;
    bool showLine = true;
    float lineWidth = 0.5f;
};

// ---------------------------------------------------------------------------
// PDF Exporter
// ---------------------------------------------------------------------------
class PdfExporter {
public:
    PdfExporter();
    ~PdfExporter();

    // Configuration
    void setStyle(const PdfStyle& style);
    PdfStyle getStyle() const;
    void setPageConfig(const PdfPageConfig& pageConfig);
    PdfPageConfig getPageConfig() const;

    // Document lifecycle
    bool createDocument(const std::string& filePath);
    bool closeDocument();
    bool isDocumentOpen() const;

    // Pages
    bool addPage();
    int getPageCount() const;

    // Text writing
    bool writeTitle(const std::string& text, float x, float y);
    bool writeHeader(const std::string& text, float x, float y);
    bool writeBody(const std::string& text, float x, float y, float maxWidth);
    bool writeParagraph(const std::string& text, float x, float y, float maxWidth);

    // Tables
    bool drawTable(const PdfTable& table);
    bool drawTable(const PdfTable& table, float x, float y, float maxHeight);
    float calculateTableHeight(const PdfTable& table) const;

    // Multi-page table (auto-splits across pages)
    bool drawMultiPageTable(const PdfTable& table);

    // Headers and footers
    void setHeader(const PdfHeaderFooter& header);
    void setFooter(const PdfHeaderFooter& footer);
    void drawHeader();
    void drawFooter(int pageNum);

    // Lines and graphics
    void drawLine(float x1, float y1, float x2, float y2, float lineWidth = 0.5f);
    void drawRect(float x, float y, float w, float h, float r = 0.0f, float g = 0.0f, float b = 0.0f);
    void fillRect(float x, float y, float w, float h, float r = 0.0f, float g = 0.0f, float b = 0.0f);

    // Images
    bool drawImage(const std::string& imagePath, float x, float y, float width, float height);

    // Utility
    float getPageWidth() const;
    float getPageHeight() const;
    float getTextWidth(const std::string& text, float fontSize) const;
    float getTextHeight(const std::string& text, float maxWidth, float fontSize) const;

    // Current Y position tracking
    float getCurrentY() const { return m_currentY; }
    void setCurrentY(float y) { m_currentY = y; }
    void advanceY(float delta) { m_currentY -= delta; }
    bool checkPageBreak(float requiredHeight);

    // Styles
    void pushStyle(const PdfStyle& style);
    void popStyle();

private:
    bool initHaru();
    void setupFonts();
    float getLineHeight(float fontSize) const;
    void drawTableHeader(HPDF_Page page, const PdfTable& table, float x, float y, float colWidth);
    void drawTableRow(HPDF_Page page, const PdfTable& table, const std::vector<std::string>& row,
                       float x, float y, float colWidth, bool alternate, bool highlight);
    float calculateColumnWidths(const PdfTable& table, float totalWidth,
                                  std::vector<float>& outWidths) const;

    // Document state
    HPDF_Doc m_doc = nullptr;
    HPDF_Page m_currentPage = nullptr;
    HPDF_Font m_currentFont = nullptr;
    std::string m_filePath;
    bool m_documentOpen = false;
    int m_pageCount = 0;
    float m_currentY = 0.0f;

    // Configuration
    PdfStyle m_style;
    PdfPageConfig m_pageConfig;
    PdfHeaderFooter m_header;
    PdfHeaderFooter m_footer;

    // Style stack
    std::vector<PdfStyle> m_styleStack;
};

} // namespace powsys365
