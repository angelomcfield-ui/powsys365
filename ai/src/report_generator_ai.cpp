/**
 * @file report_generator_ai.cpp
 * @brief Implementation of the AI-powered report generator.
 */

#include "powsy365/ai/report_generator_ai.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace powsy365::ai {

/* ------------------------------------------------------------------ */
/*  Construction                                                       */
/* ------------------------------------------------------------------ */

AIReportGenerator::AIReportGenerator(AIGateway& gateway, LLMProvider provider)
    : gateway_(gateway), provider_(provider) {
    model_ = gateway_.getModel(provider_);
    temperature_ = 0.3f;  // Lower temperature for factual reports
    max_tokens_ = 4096;
}

/* ------------------------------------------------------------------ */
/*  Report generation                                                  */
/* ------------------------------------------------------------------ */

std::vector<ReportSection> AIReportGenerator::generateReport(
    const ReportMetadata& metadata,
    const std::map<std::string, std::string>& analysis_data,
    bool include_recommendations
) {
    std::vector<ReportSection> sections;

    // Section 1: Executive Summary
    sections.push_back({
        "Executive Summary",
        summarizeResults(analysis_data),
        "summary",
        1
    });

    // Section 2: System Overview
    {
        std::ostringstream oss;
        oss << "System: " << metadata.system_name << "\n";
        oss << "Buses: " << metadata.bus_count << "\n";
        oss << "Branches: " << metadata.branch_count << "\n";
        oss << "Report Type: " << metadata.report_type << "\n";
        oss << "Generated: " << metadata.created_at << "\n";
        sections.push_back({
            "System Overview",
            oss.str(),
            "summary",
            2
        });
    }

    // Section 3: Detailed Findings
    {
        std::ostringstream oss;
        for (const auto& [key, value] : analysis_data) {
            oss << "### " << key << "\n" << value << "\n\n";
        }
        sections.push_back({
            "Detailed Findings",
            oss.str(),
            "findings",
            3
        });
    }

    // Section 4: AI Recommendations (optional)
    if (include_recommendations) {
        std::string findings;
        for (const auto& [key, value] : analysis_data) {
            findings += key + ": " + value + "\n";
        }

        auto actions = suggestActions(findings);
        std::ostringstream oss;
        for (size_t i = 0; i < actions.size(); ++i) {
            oss << (i + 1) << ". " << actions[i] << "\n";
        }

        sections.push_back({
            "Recommendations",
            oss.str(),
            "recommendations",
            4
        });
    }

    // Sort by order
    std::sort(sections.begin(), sections.end(),
              [](const auto& a, const auto& b) { return a.order < b.order; });

    return sections;
}

std::string AIReportGenerator::generateMarkdownReport(
    const ReportMetadata& metadata,
    const std::map<std::string, std::string>& analysis_data,
    bool include_recommendations
) {
    auto sections = generateReport(metadata, analysis_data, include_recommendations);
    return renderMarkdown(metadata, sections);
}

/* ------------------------------------------------------------------ */
/*  Summarization                                                      */
/* ------------------------------------------------------------------ */

std::string AIReportGenerator::summarizeResults(
    const std::map<std::string, std::string>& analysis_data,
    int max_length
) {
    std::string prompt = buildSummaryPrompt(analysis_data, max_length);

    ChatRequest req;
    req.provider = provider_;
    req.model = model_;
    req.messages = {{"user", prompt, ""}};
    req.temperature = temperature_;
    req.max_tokens = max_tokens_;

    ChatResponse resp = gateway_.chat(req);
    return resp.success ? resp.content : "Summary generation failed: " + resp.error_message;
}

std::string AIReportGenerator::summarizeSection(
    const std::string& section_title,
    const std::string& section_content,
    int max_length
) {
    std::ostringstream oss;
    oss << "Summarize the following section titled \"" << section_title
        << "\" in at most " << max_length << " characters.\n\n";
    oss << section_content.substr(0, 4000); // Truncate very long content

    ChatRequest req;
    req.provider = provider_;
    req.model = model_;
    req.messages = {{"user", oss.str(), ""}};
    req.temperature = temperature_;
    req.max_tokens = max_tokens_;

    ChatResponse resp = gateway_.chat(req);
    return resp.success ? resp.content : "Section summary failed.";
}

/* ------------------------------------------------------------------ */
/*  Recommendations                                                    */
/* ------------------------------------------------------------------ */

std::vector<std::string> AIReportGenerator::suggestActions(
    const std::string& findings,
    const std::string& system_context
) {
    std::string prompt = buildRecommendationsPrompt(findings, system_context);

    ChatRequest req;
    req.provider = provider_;
    req.model = model_;
    req.messages = {{"user", prompt, ""}};
    req.temperature = 0.2f;  // Very low temperature for actionable outputs
    req.max_tokens = max_tokens_;

    ChatResponse resp = gateway_.chat(req);
    if (!resp.success) {
        return {"Recommendation generation failed: " + resp.error_message};
    }

    // Parse numbered list from response
    std::vector<std::string> actions;
    std::istringstream stream(resp.content);
    std::string line;
    std::string current_action;

    while (std::getline(stream, line)) {
        // Check if line starts with a number (new action)
        if (!line.empty() && std::isdigit(line[0])) {
            if (!current_action.empty()) {
                // Trim trailing whitespace
                current_action.erase(
                    current_action.find_last_not_of(" \n\r\t") + 1
                );
                actions.push_back(current_action);
            }
            // Remove leading number and punctuation
            size_t start = line.find_first_not_of("0123456789. ") - 1;
            if (start == std::string::npos) start = 0;
            current_action = line.substr(line.find_first_not_of("0123456789. "));
        } else if (!line.empty() && !current_action.empty()) {
            current_action += " " + line;
        }
    }

    if (!current_action.empty()) {
        current_action.erase(current_action.find_last_not_of(" \n\r\t") + 1);
        actions.push_back(current_action);
    }

    // If parsing failed, return the whole response as one action
    if (actions.empty() && !resp.content.empty()) {
        actions.push_back(resp.content);
    }

    return actions;
}

std::vector<AIReportGenerator::ActionItem> AIReportGenerator::suggestStructuredActions(
    const std::string& findings,
    const std::string& system_context
) {
    auto action_strings = suggestActions(findings, system_context);
    std::vector<ActionItem> items;

    for (size_t i = 0; i < action_strings.size(); ++i) {
        ActionItem item;
        item.priority = static_cast<int>(i) + 1;
        item.description = action_strings[i];

        // Categorise based on keywords
        std::string lower = action_strings[i];
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.find("relay") != std::string::npos ||
            lower.find("protection") != std::string::npos ||
            lower.find("breaker") != std::string::npos) {
            item.category = "protection";
        } else if (lower.find("maintenance") != std::string::npos ||
                   lower.find("inspect") != std::string::npos ||
                   lower.find("replace") != std::string::npos) {
            item.category = "maintenance";
        } else if (lower.find("plan") != std::string::npos ||
                   lower.find("upgrade") != std::string::npos ||
                   lower.find("expand") != std::string::npos) {
            item.category = "planning";
        } else {
            item.category = "operational";
        }

        item.estimated_impact = "To be assessed";
        items.push_back(item);
    }

    return items;
}

/* ------------------------------------------------------------------ */
/*  Formatting                                                         */
/* ------------------------------------------------------------------ */

std::string AIReportGenerator::renderMarkdown(
    const ReportMetadata& metadata,
    const std::vector<ReportSection>& sections
) {
    std::ostringstream oss;

    oss << "# " << metadata.title << "\n\n";
    oss << "| Field | Value |\n";
    oss << "|-------|-------|\n";
    oss << "| **Author** | " << metadata.author << " |\n";
    oss << "| **Date** | " << metadata.created_at << " |\n";
    oss << "| **Type** | " << metadata.report_type << " |\n";
    oss << "| **System** | " << metadata.system_name << " |\n";
    oss << "| **Buses** | " << metadata.bus_count << " |\n";
    oss << "| **Branches** | " << metadata.branch_count << " |\n";
    oss << "\n---\n\n";

    for (const auto& section : sections) {
        oss << "## " << section.title << "\n\n";
        oss << section.content << "\n\n";
        oss << "---\n\n";
    }

    oss << "*Generated by POWSYS365 AI Report Generator*\n";

    return oss.str();
}

std::string AIReportGenerator::renderHTML(
    const ReportMetadata& metadata,
    const std::vector<ReportSection>& sections
) {
    std::ostringstream oss;

    oss << "<!DOCTYPE html>\n<html>\n<head>\n";
    oss << "<meta charset=\"UTF-8\">\n";
    oss << "<title>" << metadata.title << "</title>\n";
    oss << "<style>\n";
    oss << "body{font-family:Arial,sans-serif;max-width:900px;margin:40px auto;"
        << "padding:0 20px;color:#333}\n";
    oss << "h1{color:#2c3e50;border-bottom:3px solid #3498db;padding-bottom:10px}\n";
    oss << "h2{color:#34495e;border-bottom:1px solid #bdc3c7;padding-bottom:5px;