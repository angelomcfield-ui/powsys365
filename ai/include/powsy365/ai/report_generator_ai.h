/**
 * @file report_generator_ai.h
 * @brief AI-powered report generator for power system analysis results.
 *
 * Integrates with the AIGateway to produce narrative reports,
 * summaries, and actionable recommendations based on numerical
 * analysis results (power flow, short-circuit, stability).
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include "powsy365/ai/ai_gateway.h"

namespace powsy365::ai {

/* ------------------------------------------------------------------ */
/*  Report sections                                                    */
/* ------------------------------------------------------------------ */

struct ReportSection {
    std::string title;
    std::string content;
    std::string section_type;  // "summary", "findings", "recommendations", "appendix"
    int order = 0;
};

struct ReportMetadata {
    std::string title;
    std::string author = "POWSYS365 AI Report Generator";
    std::string created_at;
    std::string report_type;     // "power_flow", "short_circuit", "stability", "comprehensive"
    std::string system_name;
    int bus_count = 0;
    int branch_count = 0;
    std::map<std::string, std::string> custom_fields;
};

/* ------------------------------------------------------------------ */
/*  AI Report Generator                                                */
/* ------------------------------------------------------------------ */

class AIReportGenerator {
public:
    /**
     * @param gateway Reference to the AI Gateway for LLM queries.
     * @param provider Default LLM provider for report generation.
     */
    AIReportGenerator(AIGateway& gateway, LLMProvider provider = LLMProvider::DeepSeek);

    // -- Report generation --

    /**
     * Generate a complete analytical report from analysis results.
     *
     * @param metadata Report metadata (title, type, system info).
     * @param analysis_data Key-value pairs of analysis results.
     * @param include_recommendations Whether to include an AI-generated
     *        recommendations section.
     * @return Structured report with sections.
     */
    std::vector<ReportSection> generateReport(
        const ReportMetadata& metadata,
        const std::map<std::string, std::string>& analysis_data,
        bool include_recommendations = true
    );

    /**
     * Generate a complete report and render it as Markdown text.
     */
    std::string generateMarkdownReport(
        const ReportMetadata& metadata,
        const std::map<std::string, std::string>& analysis_data,
        bool include_recommendations = true
    );

    // -- Summarization --

    /**
     * Summarise analysis results into an executive summary.
     *
     * @param analysis_data Key-value result pairs.
     * @param max_length Maximum summary length in characters.
     * @return Concise summary string.
     */
    std::string summarizeResults(
        const std::map<std::string, std::string>& analysis_data,
        int max_length = 2000
    );

    /**
     * Summarise a specific section of results.
     */
    std::string summarizeSection(
        const std::string& section_title,
        const std::string& section_content,
        int max_length = 500
    );

    // -- Recommendations --

    /**
     * Suggest corrective actions based on analysis findings.
     *
     * @param findings Description of issues or anomalies found.
     * @param system_context Additional system context (topology, ratings).
     * @return Prioritised list of suggested actions.
     */
    std::vector<std::string> suggestActions(
        const std::string& findings,
        const std::string& system_context = ""
    );

    /**
     * Suggest actions and return as structured data.
     */
    struct ActionItem {
        int priority = 1;           // 1 = highest, 5 = lowest
        std::string category;       // "operational", "protection", "maintenance", "planning"
        std::string description;
        std::string estimated_impact;
        std::string responsible_party = "operations_team";
    };

    std::vector<ActionItem> suggestStructuredActions(
        const std::string& findings,
        const std::string& system_context = ""
    );

    // -- Formatting --

    /**
     * Render report sections as Markdown.
     */
    static std::string renderMarkdown(
        const ReportMetadata& metadata,
        const std::vector<ReportSection>& sections
    );

    /**
     * Render report sections as HTML.
     */
    static std::string renderHTML(
        const ReportMetadata& metadata,
        const std::vector<ReportSection>& sections
    );

    /**
     * Render report as plain text.
     */
    static std::string renderText(
        const ReportMetadata& metadata,
        const std::vector<ReportSection>& sections
    );

    // -- Configuration --

    void setProvider(LLMProvider provider);
    void setModel(const std::string& model);
    void setTemperature(float temperature);
    void setMaxTokens(int max_tokens);

private:
    AIGateway& gateway_;
    LLMProvider provider_;
    std::string model_;
    float temperature_ = 0.3f;
    int max_tokens_ = 4096;

    std::string buildAnalysisPrompt(
        const ReportMetadata& metadata,
        const std::map<std::string, std::string>& analysis_data
    ) const;

    std::string buildSummaryPrompt(
        const std::map<std::string, std::string>& analysis_data,
        int max_length
    ) const;

    std::string buildRecommendationsPrompt(
        const std::string& findings,
        const std::string& system_context
    ) const;
};

} // namespace powsy365::ai
