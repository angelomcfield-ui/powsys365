#include "powsy365/results/pdf_exporter.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <hpdf.h>

namespace powsys365 {

// ============================================================================
// PdfExporter
// ============================================================================
PdfExporter::PdfExporter() = default;

PdfExporter::~PdfExporter() {
    closeDocument();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void PdfExporter::setStyle(const PdfStyle& style) {
    m_style = style;
}

PdfStyle PdfExporter::getStyle() const {
    return m_style;
}

void PdfExporter::setPageConfig(const PdfPageConfig& pageConfig) {
    m_pageConfig = pageConfig;
}

PdfPageConfig PdfExporter::getPageConfig() const {
    return m_pageConfig;
}

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------
bool PdfExporter::createDocument(const std::string& filePath) {
    if (m_documentOpen) {
        closeDocument();
    }

    m_doc = HPDF_New(nullptr, nullptr);
    if (!m_doc) return false;

    m_filePath = filePath;
    m_pageCount = 0;
    m_currentY = 0;
    m_documentOpen = true;

    // Set page size
    HPDF_Page page = HPDF_AddPage(m_doc);
    float pw = m_pageConfig.pageWidth;
    float ph = m_pageConfig.pageHeight;
    if (m_pageConfig.landscape) std::swap(pw, ph);
    HPDF_Page_SetWidth(page, pw);
    HPDF_Page_SetHeight(page, ph);
    m_currentPage = page;
    m_pageCount = 1;
    m_currentY = ph - m_style.pageMargin;

    setupFonts();
    return true;
}

bool PdfExporter::closeDocument() {
    if (!m_documentOpen) return false;

    if (m_doc) {
        HPDF_SaveToFile(m_doc, m_filePath.c_str());
        HPDF_Free(m_doc);
        m_doc = nullptr;
    }

    m_currentPage = nullptr;
    m_documentOpen = false;
    m_pageCount = 0;
    return true;
}

bool PdfExporter::isDocumentOpen() const {
    return m_documentOpen;
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
void PdfExporter::setupFonts() {
    if (!m_doc) return;

    m_currentFont = HPDF_GetFont(m_doc, m_style.fontName.c_str(), nullptr);
    if (!m_currentFont) {
        m_currentFont = HPDF_GetFont(m_doc, "Helvetica", nullptr);
    }
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
bool PdfExporter::addPage() {
    if (!m_doc) return false;

    HPDF_Page page = HPDF_AddPage(m_doc);
    float pw = m_pageConfig.pageWidth;
    float ph = m_pageConfig.pageHeight;
    if (m_pageConfig.landscape) std::swap(pw, ph);
    HPDF_Page_SetWidth(page, pw);
    HPDF_Page_SetHeight(page, ph);

    m_currentPage = page;
    m_pageCount++;
    m_currentY = ph - m_style.pageMargin;

    // Draw header/footer
    drawHeader();

    return true;
}

int PdfExporter::getPageCount() const {
    return m_pageCount;
}

// ---------------------------------------------------------------------------
// Text writing
// ---------------------------------------------------------------------------
bool PdfExporter::writeTitle(const std::string& text, float x, float y) {
    if (!m_currentPage || !m_doc) return false;

    HPDF_Font font = HPDF_GetFont(m_doc, "Helvetica-Bold", nullptr);
    HPDF_Page_SetFontAndSize(m_currentPage, font, m_style.titleFontSize);
    HPDF_Page_SetRGBFill(m_currentPage, m_style.titleR, m_style.titleG, m_style.titleB);

    HPDF_Page_BeginText(m_currentPage);
    HPDF_Page_TextOut(m_currentPage, x, y, text.c_str());
    HPDF_Page_EndText(m_currentPage);

    return true;
}

bool PdfExporter::writeHeader(const std::string& text, float x, float y) {
    if (!m_currentPage || !m_doc) return false;

    HPDF_Font font = HPDF_GetFont(m_doc, "Helvetica-Bold", nullptr);
    HPDF_Page_SetFontAndSize(m_currentPage, font, m_style.headerFontSize);
    HPDF_Page_SetRGBFill(m_currentPage, m_style.headerR, m_style.headerG, m_style.headerB);

    HPDF_Page_BeginText(m_currentPage);
    HPDF_Page_TextOut(m_currentPage, x, y, text.c_str());
    HPDF_Page_EndText(m_currentPage);

    return true;
}

bool PdfExporter::writeBody(const std::string& text, float x, float y, float maxWidth) {
    if (!m_currentPage || !m_doc) return false;

    HPDF_Page_SetFontAndSize(m_currentPage, m_currentFont, m_style.bodyFontSize);
    HPDF_Page_SetRGBFill(m_currentPage, m_style.bodyR, m_style.bodyG, m_style.bodyB);

    HPDF_Page_BeginText(m_currentPage);
    HPDF_Page_TextOut(m_currentPage, x, y, text.c_str());
    HPDF_Page_EndText(m_currentPage);

    return true;
}

bool PdfExporter::writeParagraph(const std::string& text, float x, float y, float maxWidth) {
    if (!m_currentPage || !m_doc) return false;

    HPDF_Page_SetFontAndSize(m_currentPage, m_currentFont, m_style.bodyFontSize);
    HPDF_Page_SetRGBFill(m_currentPage, m_style.bodyR, m_style.bodyG, m_style.bodyB);

    HPDF_Page_BeginText(m_currentPage);
    HPDF_Page_TextRect(m_currentPage, x, y, x + maxWidth, y - 500, text.c_str(),
                        HPDF_TALIGN_LEFT, nullptr);
    HPDF_Page_EndText(m_currentPage);

    return true;
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------
bool PdfExporter::drawTable(const PdfTable& table) {
    float x = m_style.pageMargin;
    if (table.x > 0) x = table.x;
    float y = m_currentY;
    if (table.y > 0) y = table.y;

    float maxH = y - m_style.pageMargin;
    return drawTable(table, x, y, maxH);
}

bool PdfExporter::drawTable(const PdfTable& table, float x, float y, float maxHeight) {
    if (!m_currentPage || !m_doc) return false;

    float pageW = getPageWidth();
    float tableW = pageW - 2 * m_style.pageMargin;
    if (table.width > 0) tableW = table.width;

    int numCols = static_cast<int>(table.columns.size());
    if (numCols == 0) return false;

    std::vector<float> colWidths;
    float totalAssigned = calculateColumnWidths(table, tableW, colWidths);
    (void)totalAssigned;

    float currentY = y;
    float rowH = m_style.tableRowHeight;
    float headerH = m_style.tableHeaderHeight;

    // Draw header
    // Header background
    HPDF_Page_SetRGBFill(m_currentPage, m_style.tableHeaderBgR, m_style.tableHeaderBgG,
                            m_style.tableHeaderBgB);
    HPDF_Page_Rectangle(m_currentPage, x, currentY - headerH, tableW, headerH);
    HPDF_Page_Fill(m_currentPage);

    // Header text
    HPDF_Font headerFont = HPDF_GetFont(m_doc, "Helvetica-Bold", nullptr);
    HPDF_Page_SetFontAndSize(m_currentPage, headerFont, m_style.tableHeaderFontSize);
    HPDF_Page_SetRGBFill(m_currentPage, m_style.tableHeaderR, m_style.tableHeaderG,
                            m_style.tableHeaderB);

    float colX = x + 4;
    for (int i = 0; i < numCols; ++i) {
        HPDF_Page_BeginText(m_currentPage);
        HPDF_Page_TextOut(m_currentPage, colX, currentY - headerH + 6,
                            table.columns[i].header.c_str());
        HPDF_Page_EndText(m_currentPage);
        colX += colWidths[i];
    }

    // Header border
    HPDF_Page_SetRGBStroke(m_currentPage, 0.8f, 0.8f, 0.8f);
    HPDF_Page_SetLineWidth(m_currentPage, 0.5f);
    HPDF_Page_Rectangle(m_currentPage, x, currentY - headerH, tableW, headerH);
    HPDF_Page_Stroke(m_currentPage);

    currentY -= headerH;

    // Draw rows
    HPDF_Font bodyFont = HPDF_GetFont(m_doc, "Helvetica", nullptr);
    HPDF_Page_SetFontAndSize(m_currentPage, bodyFont, m_style.tableBodyFontSize);

    for (size_t rowIdx = 0; rowIdx < table.rows.size(); ++rowIdx) {
        // Check page break
        if (currentY - rowH < m_style.pageMargin + 30) {
            addPage();
            currentY = m_currentY;
        }

        const auto& row = table.rows[rowIdx];
        bool highlight = std::find(table.highlightRows.begin(), table.highlightRows.end(),
                                     static_cast<int>(rowIdx)) != table.highlightRows.end();
        bool alternate = (rowIdx % 2 == 1);

        // Row background
        if (highlight) {
            HPDF_Page_SetRGBFill(m_currentPage, 1.0f, 0.95f, 0.8f);
        } else if (alternate) {
            HPDF_Page_SetRGBFill(m_currentPage, m_style.tableAltBgR, m_style.tableAltBgG,
                                    m_style.tableAltBgB);
        } else {
            HPDF_Page_SetRGBFill(m_currentPage, 1.0f, 1.0f, 1.0f);
        }
        HPDF_Page_Rectangle(m_currentPage, x, currentY - rowH, tableW, rowH);
        HPDF_Page_Fill(m_currentPage);

        // Row text
        HPDF_Page_SetRGBFill(m_currentPage, m_style.bodyR, m_style.bodyG, m_style.bodyB);
        colX = x + 4;
        for (size_t i = 0; i < row.size() && i < colWidths.size(); ++i) {
            HPDF_Page_BeginText(m_currentPage);
            HPDF_Page_TextOut(m_currentPage, colX, currentY - rowH + 4, row[i].c_str());
            HPDF_Page_EndText(m_currentPage);
            colX += colWidths[i];
        }

        // Row border
        HPDF_Page_SetRGBStroke(m_currentPage, 0.85f, 0.85f, 0.85f);
        HPDF_Page_SetLineWidth(m_currentPage, 0.3f);
        HPDF_Page_MoveTo(m_currentPage, x, currentY - rowH);
        HPDF_Page_LineTo(m_currentPage, x + tableW, currentY - rowH);
        HPDF_Page_Stroke(m_currentPage);

        currentY -= rowH;
    }

    // Bottom border
    HPDF_Page_SetRGBStroke(m_currentPage, 0.5f, 0.5f, 0.5f);
    HPDF_Page_SetLineWidth(m_currentPage, 0.5f);
    HPDF_Page_MoveTo(m_currentPage, x, currentY);
    HPDF_Page_LineTo(m_currentPage, x + tableW, currentY);
    HPDF_Page_Stroke(m_currentPage);

    m_currentY = currentY - 10;
    return true;
}

float PdfExporter::calculateTableHeight(const PdfTable& table) const {
    float headerH = m_style.tableHeaderHeight;
    float rowH = m_style.tableRowHeight;
    return headerH + rowH * table.rows.size() + 10;
}

bool PdfExporter::drawMultiPageTable(const PdfTable& table) {
    if (!m_currentPage || !m_doc) return false;

    float pageH = getPageHeight();
    float available = m_currentY - m_style.pageMargin - 40; // Space for footer

    float tableH = calculateTableHeight(table);

    if (tableH <= available) {
        return drawTable(table);
    }

    // Need to split across pages
    float rowH = m_style.tableRowHeight;
    int rowsPerPage = static_cast<int>((available - m_style.tableHeaderHeight) / rowH);
    if (rowsPerPage < 1) rowsPerPage = 1;

    int totalRows = static_cast<int>(table.rows.size());
    int rowsDrawn = 0;

    while (rowsDrawn < totalRows) {
        int rowsThisPage = std::min(rowsPerPage, totalRows - rowsDrawn);

        PdfTable pageTable = table;
        pageTable.rows.clear();
        for (int i = 0; i < rowsThisPage; ++i) {
            pageTable.rows.push_back(table.rows[rowsDrawn + i]);
        }

        // Adjust highlight rows for this page
        pageTable.highlightRows.clear();
        for (int hr : table.highlightRows) {
            if (hr >= rowsDrawn && hr < rowsDrawn + rowsThisPage) {
                pageTable.highlightRows.push_back(hr - rowsDrawn);
            }
        }

        drawTable(pageTable);

        rowsDrawn += rowsThisPage;

        if (rowsDrawn < totalRows) {
            addPage();
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Headers and footers
// ---------------------------------------------------------------------------
void PdfExporter::setHeader(const PdfHeaderFooter& header) {
    m_header = header;
}

void PdfExporter::setFooter(const PdfHeaderFooter& footer) {
    m_footer = footer;
}

void PdfExporter::drawHeader() {
    if (!m_currentPage || !m_doc) return;

    HPDF_Font font = HPDF_GetFont(m_doc, "Helvetica", nullptr);
    HPDF_Page_SetFontAndSize(m_currentPage, font, m_style.footerFontSize);
    HPDF_Page_SetRGBFill(m_currentPage, 0.4f, 0.4f, 0.4f);

    float pageW = getPageWidth();
    float y = getPageHeight() - 25;

    // Left
    if (!m_header.leftText.empty()) {
        HPDF_Page_BeginText(m_currentPage);
        HPDF_Page_TextOut(m_currentPage, m_style.pageMargin, y, m_header.leftText.c_str());
        HPDF_Page_EndText(m_currentPage);
    }

    // Center
    if (!m_header.centerText.empty()) {
        float tw = HPDF_Page_TextWidth(m_currentPage, m_header.centerText.c_str());
        HPDF_Page_BeginText(m_currentPage);
        HPDF_Page_TextOut(m_currentPage, (pageW - tw) / 2, y, m_header.centerText.c_str());
        HPDF_Page_EndText(m_currentPage);
    }

    // Right
    if (!m_header.rightText.empty()) {
        float tw = HPDF_Page_TextWidth(m_currentPage, m_header.rightText.c_str());
        HPDF_Page_BeginText(m_currentPage);
        HPDF_Page_TextOut(m_currentPage, pageW - m_style.pageMargin - tw, y,
                            m_header.rightText.c_str());
        HPDF_Page_EndText(m_currentPage);
    }

    // Line
    if (m_header.showLine) {
        HPDF_Page_SetLineWidth(m_currentPage, m_header.lineWidth);
        HPDF_Page_SetRGBStroke(m_currentPage, m_style.lineR, m_style.lineG, m_style.lineB);
        HPDF_Page_MoveTo(m_currentPage, m_style.pageMargin, y - 5);
        HPDF_Page_LineTo(m_currentPage, pageW - m_style.pageMargin, y - 5);
        HPDF_Page_Stroke(m_currentPage);
    }
}

void PdfExporter::drawFooter(int pageNum) {
    if (!m_currentPage || !m_doc) return;

    HPDF_Font font = HPDF_GetFont(m_doc, "Helvetica", nullptr);
    HPDF_Page_SetFontAndSize(m_currentPage, font, m_style.footerFontSize);
    HPDF_Page_SetRGBFill(m_currentPage, 0.4f, 0.4f, 0.4f);

    float pageW = getPageWidth();
    float y = 25;

    // Left
    if (!m_footer.leftText.empty()) {
        HPDF_Page_BeginText(m_currentPage);
        HPDF_Page_TextOut(m_currentPage, m_style.pageMargin, y, m_footer.leftText.c_str());
        HPDF_Page_EndText(m_currentPage);
    }

    // Center
    if (!m_footer.centerText.empty()) {
        float tw = HPDF_Page_TextWidth(m_currentPage, m_footer.centerText.c_str());
        HPDF_Page_BeginText(m_currentPage);
        HPDF_Page_TextOut(m_currentPage, (pageW - tw) / 2, y, m_footer.centerText.c_str());
        HPDF_Page_EndText(m_currentPage);
    }

    // Right (page number)
    if (!m_footer.rightText.empty()) {
        std::string rightText = m_footer.rightText;
        size_t pnPos = rightText.find("{p}");
        if (pnPos != std::string::npos) {
            rightText.replace(pnPos, 3, std::to_string(pageNum));
        }
        float tw = HPDF_Page_TextWidth(m_currentPage, rightText.c_str());
        HPDF_Page_BeginText(m_currentPage);
        HPDF_Page_TextOut(m_currentPage, pageW - m_style.pageMargin - tw, y, rightText.c_str());
        HPDF_Page_EndText(m_currentPage);
    }

    // Line
    if (m_footer.showLine) {
        HPDF_Page_SetLineWidth(m_currentPage, m_footer.lineWidth);
        HPDF_Page_SetRGBStroke(m_currentPage, m_style.lineR, m_style.lineG, m_style.lineB);
        HPDF_Page_MoveTo(m_currentPage, m_style.pageMargin, y + 10);
        HPDF_Page_LineTo(m_currentPage, pageW - m_style.pageMargin, y + 10);
        HPDF_Page_Stroke(m_currentPage);
    }
}

// ---------------------------------------------------------------------------
// Lines and graphics
// ---------------------------------------------------------------------------
void PdfExporter::drawLine(float x1, float y1, float x2, float y2, float lineWidth) {
    if (!m_currentPage) return;

    HPDF_Page_SetLineWidth(m_currentPage, lineWidth);
    HPDF_Page_MoveTo(m_currentPage, x1, y1);
    HPDF_Page_LineTo(m_currentPage, x2, y2);
    HPDF_Page_Stroke(m_currentPage);
}

void PdfExporter::drawRect(float x, float y, float w, float h, float r, float g, float b) {
    if (!m_currentPage) return;

    HPDF_Page_SetRGBStroke(m_currentPage, r, g, b);
    HPDF_Page_Rectangle(m_currentPage, x, y, w, h);
    HPDF_Page_Stroke(m_currentPage);
}

void PdfExporter::fillRect(float x, float y, float w, float h, float r, float g, float b) {
    if (!m_currentPage) return;

    HPDF_Page_SetRGBFill(m_currentPage, r, g, b);
    HPDF_Page_Rectangle(m_currentPage, x, y, w, h);
    HPDF_Page_Fill(m_currentPage);
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------
bool PdfExporter::drawImage(const std::string& imagePath, float x, float y,
                              float width, float height) {
    if (!m_currentPage || !m_doc) return false;

    HPDF_Image image = HPDF_LoadJpegImageFromFile(m_doc, imagePath.c_str());
    if (!image) {
        image = HPDF_LoadPngImageFromFile(m_doc, imagePath.c_str());
    }
    if (!image) return false;

    HPDF_Page_DrawImage(m_currentPage, image, x, y, width, height);
    return true;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
float PdfExporter::getPageWidth() const {
    float pw = m_pageConfig.pageWidth;
    if (m_pageConfig.landscape) pw = m_pageConfig.pageHeight;
    return pw;
}

float PdfExporter::getPageHeight() const {
    float ph = m_pageConfig.pageHeight;
    if (m_pageConfig.landscape) ph = m_pageConfig.pageWidth;
    return ph;
}

float PdfExporter::getTextWidth(const std::string& text, float fontSize) const {
    if (!m_currentPage || !m_doc) return 0;

    HPDF_Font font = m_currentFont;
    HPDF_Page_SetFontAndSize(m_currentPage, font, fontSize);
    return HPDF_Page_TextWidth(m_currentPage, text.c_str());
}

float PdfExporter::getTextHeight(const std::string&, float maxWidth, float fontSize) const {
    float lineH = getLineHeight(fontSize);
    // Rough estimate
    return lineH * 2;
}

float PdfExporter::getLineHeight(float fontSize) const {
    return fontSize * m_style.lineSpacing;
}

bool PdfExporter::checkPageBreak(float requiredHeight) {
    if (m_currentY - requiredHeight < m_style.pageMargin + 30) {
        addPage();
        return true;
    }
    return false;
}

void PdfExporter::pushStyle(const PdfStyle& style) {
    m_styleStack.push_back(m_style);
    m_style = style;
    setupFonts();
}

void PdfExporter::popStyle() {
    if (!m_styleStack.empty()) {
        m_style = m_styleStack.back();
        m_styleStack.pop_back();
        setupFonts();
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
float PdfExporter::calculateColumnWidths(const PdfTable& table, float totalWidth,
                                           std::vector<float>& outWidths) const {
    int numCols = static_cast<int>(table.columns.size());
    outWidths.resize(numCols, 0);

    float assignedWidth = 0;
    int autoCols = 0;

    for (int i = 0; i < numCols; ++i) {
        if (table.columns[i].width > 0) {
            outWidths[i] = table.columns[i].width;
            assignedWidth += outWidths[i];
        } else {
            autoCols++;
        }
    }

    if (autoCols > 0) {
        float autoWidth = (totalWidth - assignedWidth) / autoCols;
        for (int i = 0; i < numCols; ++i) {
            if (outWidths[i] == 0) {
                outWidths[i] = autoWidth;
            }
        }
    }

    return assignedWidth;
}

} // namespace powsys365
