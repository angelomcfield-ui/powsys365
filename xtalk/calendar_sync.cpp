#include "calendar_sync.h"
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>

namespace powsys365::xtalk {

// ============================================================================
// Utilidades de tiempo
// ============================================================================

std::string CalendarSync::formatToRfc3339(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    if (ms.count() > 0) {
        oss << "." << std::setw(3) << std::setfill('0') << ms.count();
    }
    oss << "Z";
    return oss.str();
}

std::chrono::system_clock::time_point CalendarSync::parseFromRfc3339(const std::string& str) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) return std::chrono::system_clock::now();
    auto tp = std::chrono::system_clock::from_time_t(timegm(&tm));
    return tp;
}

std::string CalendarSync::formatToIcsTimestamp(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return oss.str();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

CalendarSync::CalendarSync() {}

CalendarSync::~CalendarSync() {
    // Detener todos los hilos de auto-sync
    for (auto& [id, flag] : syncThreadFlags_) {
        flag = false;
    }
    for (auto& [id, thread] : syncThreads_) {
        if (thread.joinable()) thread.join();
    }
}

// ============================================================================
// Validacion
// ============================================================================

bool CalendarSync::calendarExists(int calendarId) const {
    std::lock_guard<std::mutex> lock(calendarsMutex_);
    return calendars_.find(calendarId) != calendars_.end();
}

bool CalendarSync::eventExists(int eventId) const {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    return events_.find(eventId) != events_.end();
}

// ============================================================================
// Notificaciones
// ============================================================================

void CalendarSync::notifyEventAdded(const CalendarEvent& event) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (eventAddedCallback_) {
        try { eventAddedCallback_(event); } catch (...) {}
    }
}

void CalendarSync::notifyEventUpdated(const CalendarEvent& event) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (eventUpdatedCallback_) {
        try { eventUpdatedCallback_(event); } catch (...) {}
    }
}

void CalendarSync::notifyEventRemoved(const CalendarEvent& event) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (eventRemovedCallback_) {
        try { eventRemovedCallback_(event); } catch (...) {}
    }
}

void CalendarSync::notifySyncCompleted(int calendarId, const SyncResult& result) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (syncCompletedCallback_) {
        try { syncCompletedCallback_(calendarId, result); } catch (...) {}
    }
}

// ============================================================================
// Gestion de calendarios
// ============================================================================

int CalendarSync::createCalendar(const std::string& name,
                                  const std::string& description,
                                  CalendarProvider provider) {
    std::lock_guard<std::mutex> lock(calendarsMutex_);
    int cid = nextCalendarId_.fetch_add(1);
    Calendar cal;
    cal.calendarId = cid;
    cal.name = name;
    cal.description = description;
    cal.provider = provider;
    cal.syncEnabled = false;
    cal.direction = SyncDirection::BIDIRECTIONAL;
    cal.lastSyncedAt = std::chrono::system_clock::now();
    calendars_[cid] = cal;
    return cid;
}

bool CalendarSync::removeCalendar(int calendarId) {
    disableAutoSync(calendarId);

    {
        std::lock_guard<std::mutex> lock(eventsMutex_);
        for (auto it = events_.begin(); it != events_.end();) {
            if (it->second.source == CalendarProvider::INTERNAL ||
                calendars_[calendarId].provider == CalendarProvider::INTERNAL) {
                // Solo eliminar eventos del calendario
                // (en produccion se marcarian como eliminados)
            }
            ++it;
        }
    }

    std::lock_guard<std::mutex> lock(calendarsMutex_);
    return calendars_.erase(calendarId) > 0;
}

bool CalendarSync::configureGoogleCalendar(int calendarId,
                                            const std::string& apiKey,
                                            const std::string& calendarId_) {
    std::lock_guard<std::mutex> lock(calendarsMutex_);
    auto it = calendars_.find(calendarId);
    if (it == calendars_.end()) return false;
    it->second.provider = CalendarProvider::GOOGLE_CALENDAR;
    it->second.externalCalendarId = calendarId_;
    googleApiKeys_[calendarId] = apiKey;
    googleCalendarIds_[calendarId] = calendarId_;
    return true;
}

bool CalendarSync::configureOutlook(int calendarId,
                                     const std::string& clientId,
                                     const std::string& clientSecret,
                                     const std::string& refreshToken) {
    std::lock_guard<std::mutex> lock(calendarsMutex_);
    auto it = calendars_.find(calendarId);
    if (it == calendars_.end()) return false;
    it->second.provider = CalendarProvider::MICROSOFT_OUTLOOK;
    outlookClientIds_[calendarId] = clientId;
    outlookClientSecrets_[calendarId] = clientSecret;
    outlookRefreshTokens_[calendarId] = refreshToken;
    return true;
}

std::optional<Calendar> CalendarSync::getCalendar(int calendarId) const {
    std::lock_guard<std::mutex> lock(calendarsMutex_);
    auto it = calendars_.find(calendarId);
    if (it == calendars_.end()) return std::nullopt;
    return it->second;
}

std::vector<Calendar> CalendarSync::listCalendars() const {
    std::lock_guard<std::mutex> lock(calendarsMutex_);
    std::vector<Calendar> result;
    for (const auto& [id, cal] : calendars_) {
        result.push_back(cal);
    }
    return result;
}

// ============================================================================
// Gestion de eventos
// ============================================================================

int CalendarSync::addEvent(int calendarId,
                           const std::string& title,
                           const std::string& description,
                           const std::string& location,
                           std::chrono::system_clock::time_point start,
                           std::chrono::system_clock::time_point end,
                           bool isAllDay,
                           const RecurrenceRule& recurrence) {
    if (!calendarExists(calendarId)) return -1;

    std::lock_guard<std::mutex> lock(eventsMutex_);
    int eid = nextEventId_.fetch_add(1);
    CalendarEvent event;
    event.eventId = eid;
    event.title = title;
    event.description = description;
    event.location = location;
    event.startTime = start;
    event.endTime = end;
    event.isAllDay = isAllDay;
    event.status = EventStatus::CONFIRMED;
    event.recurrence = recurrence;
    event.source = CalendarProvider::INTERNAL;
    event.createdAt = std::chrono::system_clock::now();
    event.updatedAt = event.createdAt;
    events_[eid] = event;

    notifyEventAdded(event);
    return eid;
}

bool CalendarSync::updateEvent(int eventId,
                                const std::string& title,
                                const std::string& description,
                                const std::string& location,
                                std::chrono::system_clock::time_point start,
                                std::chrono::system_clock::time_point end) {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    auto it = events_.find(eventId);
    if (it == events_.end()) return false;

    it->second.title = title;
    it->second.description = description;
    it->second.location = location;
    it->second.startTime = start;
    it->second.endTime = end;
    it->second.updatedAt = std::chrono::system_clock::now();

    notifyEventUpdated(it->second);
    return true;
}

bool CalendarSync::removeEvent(int eventId) {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    auto it = events_.find(eventId);
    if (it == events_.end()) return false;
    auto event = it->second;
    events_.erase(it);
    notifyEventRemoved(event);
    return true;
}

bool CalendarSync::cancelEvent(int eventId) {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    auto it = events_.find(eventId);
    if (it == events_.end()) return false;
    it->second.status = EventStatus::CANCELLED;
    it->second.updatedAt = std::chrono::system_clock::now();
    notifyEventUpdated(it->second);
    return true;
}

std::optional<CalendarEvent> CalendarSync::getEvent(int eventId) const {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    auto it = events_.find(eventId);
    if (it == events_.end()) return std::nullopt;
    return it->second;
}

std::vector<CalendarEvent> CalendarSync::getEvents(int calendarId,
                                                    std::chrono::system_clock::time_point from,
                                                    std::chrono::system_clock::time_point to) const {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    std::vector<CalendarEvent> result;
    for (const auto& [id, event] : events_) {
        // Eventos que se solapan con el rango
        if (event.startTime < to && event.endTime > from &&
            event.status != EventStatus::CANCELLED) {
            result.push_back(event);
        }
    }
    std::sort(result.begin(), result.end(),
        [](const CalendarEvent& a, const CalendarEvent& b) {
            return a.startTime < b.startTime;
        });
    return result;
}

std::vector<CalendarEvent> CalendarSync::getEventsForToday(int calendarId) const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&t);
    tm->tm_hour = 0; tm->tm_min = 0; tm->tm_sec = 0;
    auto startOfDay = std::chrono::system_clock::from_time_t(std::mktime(tm));
    auto endOfDay = startOfDay + std::chrono::hours(24);
    return getEvents(calendarId, startOfDay, endOfDay);
}

std::vector<CalendarEvent> CalendarSync::getEventsForWeek(int calendarId) const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&t);
    int daysSinceSunday = tm->tm_wday;
    tm->tm_hour = 0; tm->tm_min = 0; tm->tm_sec = 0;
    auto startOfWeek = std::chrono::system_clock::from_time_t(std::mktime(tm)) -
                        std::chrono::hours(24 * daysSinceSunday);
    auto endOfWeek = startOfWeek + std::chrono::hours(24 * 7);
    return getEvents(calendarId, startOfWeek, endOfWeek);
}

std::vector<CalendarEvent> CalendarSync::searchEvents(const std::string& query) const {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    std::vector<CalendarEvent> result;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    for (const auto& [id, event] : events_) {
        std::string title = event.title;
        std::string desc = event.description;
        std::transform(title.begin(), title.end(), title.begin(), ::tolower);
        std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);

        if (title.find(lowerQuery) != std::string::npos ||
            desc.find(lowerQuery) != std::string::npos ||
            event.location.find(lowerQuery) != std::string::npos) {
            result.push_back(event);
        }
    }
    return result;
}

// ============================================================================
// Attendees
// ============================================================================

bool CalendarSync::addAttendee(int eventId, const std::string& email) {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    auto it = events_.find(eventId);
    if (it == events_.end()) return false;
    if (std::find(it->second.attendees.begin(), it->second.attendees.end(), email)
        == it->second.attendees.end()) {
        it->second.attendees.push_back(email);
        it->second.updatedAt = std::chrono::system_clock::now();
    }
    return true;
}

bool CalendarSync::removeAttendee(int eventId, const std::string& email) {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    auto it = events_.find(eventId);
    if (it == events_.end()) return false;
    auto& attendees = it->second.attendees;
    auto ait = std::find(attendees.begin(), attendees.end(), email);
    if (ait != attendees.end()) {
        attendees.erase(ait);
        it->second.updatedAt = std::chrono::system_clock::now();
    }
    return true;
}

// ============================================================================
// iCalendar (RFC 5545) Exportacion
// ============================================================================

std::string CalendarSync::generateIcsHeader() const {
    return "BEGIN:VCALENDAR\r\n"
           "VERSION:2.0\r\n"
           "PRODID:-//POWSYS365//CalendarSync//EN\r\n"
           "CALSCALE:GREGORIAN\r\n"
           "METHOD:PUBLISH\r\n";
}

std::string CalendarSync::generateIcsFooter() const {
    return "END:VCALENDAR\r\n";
}

std::string CalendarSync::recurrenceToIcs(const RecurrenceRule& rr) const {
    if (rr.type == RecurrenceType::NONE) return "";

    std::string freq;
    switch (rr.type) {
        case RecurrenceType::DAILY:   freq = "DAILY"; break;
        case RecurrenceType::WEEKLY:  freq = "WEEKLY"; break;
        case RecurrenceType::MONTHLY: freq = "MONTHLY"; break;
        case RecurrenceType::YEARLY:  freq = "YEARLY"; break;
        default: return "";
    }

    std::string result = "RRULE:FREQ=" + freq;
    if (rr.interval > 1) result += ";INTERVAL=" + std::to_string(rr.interval);
    if (rr.occurrences > 0) result += ";COUNT=" + std::to_string(rr.occurrences);
    if (!rr.byDay.empty()) {
        const char* days[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
        result += ";BYDAY=";
        for (size_t i = 0; i < rr.byDay.size(); ++i) {
            if (i > 0) result += ",";
            result += days[rr.byDay[i] % 7];
        }
    }
    result += "\r\n";
    return result;
}

std::string CalendarSync::generateIcsEvent(const CalendarEvent& event) const {
    std::ostringstream oss;
    oss << "BEGIN:VEVENT\r\n";
    oss << "UID:powsys365-" << event.eventId << "@powsys365.local\r\n";
    oss << "DTSTAMP:" << formatToIcsTimestamp(event.createdAt) << "\r\n";

    if (event.isAllDay) {
        auto t = std::chrono::system_clock::to_time_t(event.startTime);
        std::tm tm;
        gmtime_r(&t, &tm);
        oss << "DTSTART;VALUE=DATE:" << std::put_time(&tm, "%Y%m%d") << "\r\n";

        t = std::chrono::system_clock::to_time_t(event.endTime);
        gmtime_r(&t, &tm);
        oss << "DTEND;VALUE=DATE:" << std::put_time(&tm, "%Y%m%d") << "\r\n";
    } else {
        oss << "DTSTART:" << formatToIcsTimestamp(event.startTime) << "\r\n";
        oss << "DTEND:" << formatToIcsTimestamp(event.endTime) << "\r\n";
    }

    oss << "SUMMARY:" << event.title << "\r\n";
    if (!event.description.empty()) {
        // Escape caracteres en ICS
        std::string desc = event.description;
        size_t pos = 0;
        while ((pos = desc.find("\n", pos)) != std::string::npos) {
            desc.replace(pos, 1, "\\n");
            pos += 2;
        }
        oss << "DESCRIPTION:" << desc << "\r\n";
    }
    if (!event.location.empty()) {
        oss << "LOCATION:" << event.location << "\r\n";
    }

    switch (event.status) {
        case EventStatus::CONFIRMED: oss << "STATUS:CONFIRMED\r\n"; break;
        case EventStatus::TENTATIVE: oss << "STATUS:TENTATIVE\r\n"; break;
        case EventStatus::CANCELLED: oss << "STATUS:CANCELLED\r\n"; break;
    }

    // Recurrencia
    std::string rrule = recurrenceToIcs(event.recurrence);
    if (!rrule.empty()) {
        oss << rrule;
    }

    // Attendees
    for (const auto& attendee : event.attendees) {
        oss << "ATTENDEE:MAILTO:" << attendee << "\r\n";
    }

    if (!event.organizer.empty()) {
        oss << "ORGANIZER:MAILTO:" << event.organizer << "\r\n";
    }

    oss << "END:VEVENT\r\n";
    return oss.str();
}

std::string CalendarSync::eventToIcs(const CalendarEvent& event) const {
    std::ostringstream oss;
    oss << generateIcsHeader();
    oss << generateIcsEvent(event);
    oss << generateIcsFooter();
    return oss.str();
}

std::string CalendarSync::exportToIcs(int calendarId) const {
    std::lock_guard<std::mutex> calLock(calendarsMutex_);
    auto calIt = calendars_.find(calendarId);
    if (calIt == calendars_.end()) return "";

    std::ostringstream oss;
    oss << generateIcsHeader();
    oss << "X-WR-CALNAME:" << calIt->second.name << "\r\n";
    if (!calIt->second.description.empty()) {
        oss << "X-WR-CALDESC:" << calIt->second.description << "\r\n";
    }

    std::lock_guard<std::mutex> evtLock(eventsMutex_);
    for (const auto& [id, event] : events_) {
        oss << generateIcsEvent(event);
    }
    oss << generateIcsFooter();
    return oss.str();
}

// ============================================================================
// iCalendar Importacion
// ============================================================================

std::string CalendarSync::extractIcsProperty(const std::string& block, const std::string& prop) {
    std::string search = prop;
    size_t pos = block.find(search);
    if (pos == std::string::npos) return "";

    size_t colonPos = block.find(':', pos);
    if (colonPos == std::string::npos) return "";

    // Buscar hasta fin de linea
    size_t endPos = block.find("\r\n", colonPos);
    if (endPos == std::string::npos) {
        endPos = block.find("\n", colonPos);
    }
    if (endPos == std::string::npos) endPos = block.length();

    return block.substr(colonPos + 1, endPos - colonPos - 1);
}

std::chrono::system_clock::time_point CalendarSync::parseIcsTimestamp(const std::string& str) {
    std::tm tm = {};
    std::istringstream iss(str);
    if (str.find('T') != std::string::npos) {
        // Formato: 20240115T120000Z
        iss >> std::get_time(&tm, "%Y%m%dT%H%M%SZ");
    } else {
        // Formato fecha sola
        iss >> std::get_time(&tm, "%Y%m%d");
    }
    if (iss.fail()) return std::chrono::system_clock::now();
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

CalendarEvent CalendarSync::parseIcsEventBlock(const std::string& block, int calendarId) {
    CalendarEvent event;
    event.eventId = nextEventId_.fetch_add(1);
    event.source = CalendarProvider::INTERNAL;
    event.status = EventStatus::CONFIRMED;
    event.createdAt = std::chrono::system_clock::now();
    event.updatedAt = event.createdAt;

    event.title = extractIcsProperty(block, "SUMMARY");
    event.description = extractIcsProperty(block, "DESCRIPTION");
    event.location = extractIcsProperty(block, "LOCATION");
    event.organizer = extractIcsProperty(block, "ORGANIZER");

    // Parsear timestamps
    std::string dtstart = extractIcsProperty(block, "DTSTART");
    if (!dtstart.empty()) {
        event.startTime = parseIcsTimestamp(dtstart);
    }
    std::string dtend = extractIcsProperty(block, "DTEND");
    if (!dtend.empty()) {
        event.endTime = parseIcsTimestamp(dtend);
    } else {
        event.endTime = event.startTime + std::chrono::hours(1);
    }

    // Status
    std::string status = extractIcsProperty(block, "STATUS");
    if (status == "TENTATIVE") event.status = EventStatus::TENTATIVE;
    else if (status == "CANCELLED") event.status = EventStatus::CANCELLED;

    // Recurrencia basica
    std::string rrule = extractIcsProperty(block, "RRULE");
    if (!rrule.empty()) {
        if (rrule.find("FREQ=DAILY") != std::string::npos) event.recurrence.type = RecurrenceType::DAILY;
        else if (rrule.find("FREQ=WEEKLY") != std::string::npos) event.recurrence.type = RecurrenceType::WEEKLY;
        else if (rrule.find("FREQ=MONTHLY") != std::string::npos) event.recurrence.type = RecurrenceType::MONTHLY;
        else if (rrule.find("FREQ=YEARLY") != std::string::npos) event.recurrence.type = RecurrenceType::YEARLY;
    }

    // Attendees
    size_t pos = 0;
    while ((pos = block.find("ATTENDEE", pos)) != std::string::npos) {
        std::string attendee = extractIcsProperty(block.substr(pos), "ATTENDEE");
        if (!attendee.empty()) {
            // Limpiar MAILTO:
            if (attendee.find("MAILTO:") == 0) {
                attendee = attendee.substr(7);
            }
            event.attendees.push_back(attendee);
        }
        pos++;
    }

    event.icsData = block;
    return event;
}

std::vector<CalendarEvent> CalendarSync::parseIcsData(const std::string& icsData, int calendarId) {
    std::vector<CalendarEvent> result;
    size_t pos = 0;
    while ((pos = icsData.find("BEGIN:VEVENT", pos)) != std::string::npos) {
        size_t endPos = icsData.find("END:VEVENT", pos);
        if (endPos == std::string::npos) break;

        std::string block = icsData.substr(pos, endPos - pos + 10);
        CalendarEvent event = parseIcsEventBlock(block, calendarId);
        result.push_back(event);

        pos = endPos + 1;
    }
    return result;
}

std::vector<CalendarEvent> CalendarSync::importFromIcs(const std::string& icsData, int calendarId) {
    auto imported = parseIcsData(icsData, calendarId);
    std::lock_guard<std::mutex> lock(eventsMutex_);
    for (auto& event : imported) {
        events_[event.eventId] = event;
    }
    return imported;
}

// ============================================================================
// Sincronizacion
// ============================================================================

SyncResult CalendarSync::sync(int calendarId) {
    std::lock_guard<std::mutex> lock(calendarsMutex_);
    auto it = calendars_.find(calendarId);
    if (it == calendars_.end()) {
        return SyncResult{false, 0, 0, 0, "Calendario no encontrado", std::chrono::system_clock::now()};
    }

    switch (it->second.provider) {
        case CalendarProvider::GOOGLE_CALENDAR:
            return syncGoogle(calendarId);
        case CalendarProvider::MICROSOFT_OUTLOOK:
            return syncOutlook(calendarId);
        default:
            return SyncResult{false, 0, 0, 0, "Proveedor no soportado",
                              std::chrono::system_clock::now()};
    }
}

// ============================================================================
// Google Calendar Sync
// ============================================================================

SyncResult CalendarSync::syncGoogle(int calendarId) {
    auto apiKeyIt = googleApiKeys_.find(calendarId);
    auto calIdIt = googleCalendarIds_.find(calendarId);
    if (apiKeyIt == googleApiKeys_.end() || calIdIt == googleCalendarIds_.end()) {
        return SyncResult{false, 0, 0, 0, "Google Calendar no configurado",
                          std::chrono::system_clock::now()};
    }
    return fetchGoogleEvents(calendarId, apiKeyIt->second, calIdIt->second);
}

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    data->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

SyncResult CalendarSync::fetchGoogleEvents(int calendarId,
                                            const std::string& apiKey,
                                            const std::string& googleCalId) {
    SyncResult result;
    result.success = true;
    result.syncTime = std::chrono::system_clock::now();

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.success = false;
        result.errorMessage = "No se pudo inicializar CURL";
        return result;
    }

    auto now = std::chrono::system_clock::now();
    auto timeMin = formatToRfc3339(now - std::chrono::hours(24 * 365));
    auto timeMax = formatToRfc3339(now + std::chrono::hours(24 * 365));

    std::string url = "https://www.googleapis.com/calendar/v3/calendars/"
                      + googleCalId + "/events?key=" + apiKey
                      + "&timeMin=" + timeMin + "&timeMax=" + timeMax
                      + "&singleEvents=true&maxResults=2500";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200) {
        result.success = false;
        result.errorMessage = "Error HTTP " + std::to_string(httpCode);
        notifySyncCompleted(calendarId, result);
        return result;
    }

    // Parsear respuesta JSON simple
    std::lock_guard<std::mutex> lock(eventsMutex_);
    size_t pos = 0;
    while ((pos = response.find("\"summary\":", pos)) != std::string::npos) {
        // Extraer titulo
        size_t titleStart = response.find("\"", pos + 10);
        if (titleStart == std::string::npos) break;
        size_t titleEnd = response.find("\"", titleStart + 1);
        if (titleEnd == std::string::npos) break;
        std::string title = response.substr(titleStart + 1, titleEnd - titleStart - 1);

        // Extracer start time
        size_t startPos = response.find("\"dateTime\":", titleEnd);
        std::chrono::system_clock::time_point start = now;
        if (startPos != std::string::npos) {
            size_t tsStart = response.find("\"", startPos + 11);
            size_t tsEnd = response.find("\"", tsStart + 1);
            if (tsStart != std::string::npos && tsEnd != std::string::npos) {
                start = parseFromRfc3339(response.substr(tsStart + 1, tsEnd - tsStart - 1));
            }
        }

        int eid = nextEventId_.fetch_add(1);
        CalendarEvent event;
        event.eventId = eid;
        event.title = title;
        event.startTime = start;
        event.endTime = start + std::chrono::hours(1);
        event.source = CalendarProvider::GOOGLE_CALENDAR;
        event.status = EventStatus::CONFIRMED;
        event.createdAt = now;
        event.updatedAt = now;
        events_[eid] = event;
        result.eventsAdded++;

        pos = titleEnd;
    }

    {
        std::lock_guard<std::mutex> calLock(calendarsMutex_);
        auto it = calendars_.find(calendarId);
        if (it != calendars_.end()) {
            it->second.lastSyncedAt = result.syncTime;
        }
    }

    notifySyncCompleted(calendarId, result);
    return result;
}

// ============================================================================
// Outlook Sync
// ============================================================================

std::string CalendarSync::refreshOutlookToken(int calendarId) {
    auto clientIdIt = outlookClientIds_.find(calendarId);
    auto clientSecretIt = outlookClientSecrets_.find(calendarId);
    auto refreshTokenIt = outlookRefreshTokens_.find(calendarId);

    if (clientIdIt == outlookClientIds_.end() ||
        clientSecretIt == outlookClientSecrets_.end() ||
        refreshTokenIt == outlookRefreshTokens_.end()) {
        return "";
    }

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string postData = "client_id=" + clientIdIt->second
                         + "&client_secret=" + clientSecretIt->second
                         + "&refresh_token=" + refreshTokenIt->second
                         + "&grant_type=refresh_token"
                         + "&scope=https://graph.microsoft.com/Calendars.ReadWrite";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://login.microsoftonline.com/common/oauth2/v2.0/token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    // Extraer access_token de respuesta
    size_t tokenPos = response.find("\"access_token\":\"");
    if (tokenPos == std::string::npos) return "";
    size_t tokenStart = response.find("\"", tokenPos + 16);
    size_t tokenEnd = response.find("\"", tokenStart + 1);
    if (tokenStart == std::string::npos || tokenEnd == std::string::npos) return "";
    return response.substr(tokenStart + 1, tokenEnd - tokenStart - 1);
}

SyncResult CalendarSync::syncOutlook(int calendarId) {
    SyncResult result;
    result.success = true;
    result.syncTime = std::chrono::system_clock::now();

    std::string accessToken = refreshOutlookToken(calendarId);
    if (accessToken.empty()) {
        result.success = false;
        result.errorMessage = "No se pudo obtener token de acceso";
        return result;
    }

    return fetchOutlookEvents(calendarId, accessToken);
}

SyncResult CalendarSync::fetchOutlookEvents(int calendarId,
                                               const std::string& accessToken) {
    SyncResult result;
    result.success = true;
    result.syncTime = std::chrono::system_clock::now();

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.success = false;
        result.errorMessage = "No se pudo inicializar CURL";
        return result;
    }

    auto now = std::chrono::system_clock::now();
    std::string startDate = formatToRfc3339(now - std::chrono::hours(24 * 365));
    std::string endDate = formatToRfc3339(now + std::chrono::hours(24 * 365));

    std::string url = "https://graph.microsoft.com/v1.0/me/calendarView?"
                      "startDateTime=" + startDate + "&endDateTime=" + endDate
                      + "&$top=100";

    std::string response;
    std::string authHeader = "Authorization: Bearer " + accessToken;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, authHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200) {
        result.success = false;
        result.errorMessage = "Error HTTP " + std::to_string(httpCode);
        return result;
    }

    // Parse respuesta JSON simplificada
    std::lock_guard<std::mutex> lock(eventsMutex_);
    size_t pos = 0;
    while ((pos = response.find("\"subject\":", pos)) != std::string::npos) {
        size_t titleStart = response.find("\"", pos + 10);
        if (titleStart == std::string::npos) break;
        size_t titleEnd = response.find("\"", titleStart + 1);
        if (titleEnd == std::string::npos) break;
        std::string title = response.substr(titleStart + 1, titleEnd - titleStart - 1);

        int eid = nextEventId_.fetch_add(1);
        CalendarEvent event;
        event.eventId = eid;
        event.title = title;
        event.startTime = now;
        event.endTime = now + std::chrono::hours(1);
        event.source = CalendarProvider::MICROSOFT_OUTLOOK;
        event.status = EventStatus::CONFIRMED;
        event.createdAt = now;
        event.updatedAt = now;
        events_[eid] = event;
        result.eventsAdded++;

        pos = titleEnd;
    }

    {
        std::lock_guard<std::mutex> calLock(calendarsMutex_);
        auto it = calendars_.find(calendarId);
        if (it != calendars_.end()) {
            it->second.lastSyncedAt = result.syncTime;
        }
    }

    notifySyncCompleted(calendarId, result);
    return result;
}

// ============================================================================
// Auto-sync
// ============================================================================

void CalendarSync::autoSyncLoop(int calendarId, std::chrono::seconds interval) {
    while (syncThreadFlags_[calendarId]) {
        for (int i = 0; i < interval.count() && syncThreadFlags_[calendarId]; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!syncThreadFlags_[calendarId]) break;
        sync(calendarId);
    }
}

bool CalendarSync::enableAutoSync(int calendarId, std::chrono::seconds interval) {
    disableAutoSync(calendarId);

    std::lock_guard<std::mutex> lock(syncThreadsMutex_);
    syncThreadFlags_[calendarId] = true;
    syncThreads_[calendarId] = std::thread(&CalendarSync::autoSyncLoop, this, calendarId, interval);
    return true;
}

bool CalendarSync::disableAutoSync(int calendarId) {
    std::lock_guard<std::mutex> lock(syncThreadsMutex_);
    auto it = syncThreadFlags_.find(calendarId);
    if (it != syncThreadFlags_.end()) {
        it->second = false;
    }
    auto threadIt = syncThreads_.find(calendarId);
    if (threadIt != syncThreads_.end() && threadIt->second.joinable()) {
        threadIt->second.join();
    }
    syncThreads_.erase(calendarId);
    syncThreadFlags_.erase(calendarId);
    return true;
}

// ============================================================================
// Callbacks
// ============================================================================

void CalendarSync::onEventAdded(EventCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    eventAddedCallback_ = callback;
}

void CalendarSync::onEventUpdated(EventCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    eventUpdatedCallback_ = callback;
}

void CalendarSync::onEventRemoved(EventCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    eventRemovedCallback_ = callback;
}

void CalendarSync::onSyncCompleted(SyncCallback callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    syncCompletedCallback_ = callback;
}

} // namespace powsys365::xtalk
