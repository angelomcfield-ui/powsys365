#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>
#include <atomic>
#include <thread>
#include <condition_variable>

namespace powsys365::xtalk {

// ============================================================================
// Tipos de calendario
// ============================================================================

enum class CalendarProvider {
    INTERNAL,
    GOOGLE_CALENDAR,
    MICROSOFT_OUTLOOK,
    APPLE_CALENDAR
};

enum class RecurrenceType {
    NONE,
    DAILY,
    WEEKLY,
    MONTHLY,
    YEARLY
};

enum class EventStatus {
    TENTATIVE,
    CONFIRMED,
    CANCELLED
};

enum class SyncDirection {
    INTERNAL_TO_EXTERNAL,
    EXTERNAL_TO_INTERNAL,
    BIDIRECTIONAL
};

// ============================================================================
// Estructuras
// ============================================================================

struct RecurrenceRule {
    RecurrenceType type = RecurrenceType::NONE;
    int interval = 1;           // cada N dias/semanas/meses
    int occurrences = -1;       // -1 = infinito
    std::chrono::system_clock::time_point until;
    std::vector<int> byDay;     // 0=domingo, 6=sabado (para weekly)
    int byMonthDay = 0;         // dia del mes (1-31)
};

struct CalendarEvent {
    int    eventId;
    std::string externalId;       // ID en proveedor externo
    std::string title;
    std::string description;
    std::string location;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    bool   isAllDay;
    EventStatus status;
    RecurrenceRule recurrence;
    CalendarProvider source;
    std::vector<std::string> attendees; // emails
    std::string organizer;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::string icsData;          // datos iCalendar
};

struct Calendar {
    int    calendarId;
    std::string name;
    std::string description;
    CalendarProvider provider;
    std::string externalCalendarId; // ID en Google/Outlook
    std::string syncToken;          // token para sincronizacion incremental
    std::chrono::system_clock::time_point lastSyncedAt;
    bool   syncEnabled;
    SyncDirection direction;
};

struct SyncResult {
    bool   success;
    int    eventsAdded;
    int    eventsUpdated;
    int    eventsRemoved;
    std::string errorMessage;
    std::chrono::system_clock::time_point syncTime;
};

// ============================================================================
// Callbacks
// ============================================================================

using EventCallback = std::function<void(const CalendarEvent&)>;
using SyncCallback = std::function<void(int calendarId, const SyncResult&)>;

// ============================================================================
// CalendarSync
// ============================================================================

class CalendarSync {
public:
    CalendarSync();
    ~CalendarSync();

    // --- Gestion de calendarios ---
    int  createCalendar(const std::string& name,
                        const std::string& description,
                        CalendarProvider provider);
    bool removeCalendar(int calendarId);
    bool configureGoogleCalendar(int calendarId,
                                  const std::string& apiKey,
                                  const std::string& calendarId);
    bool configureOutlook(int calendarId,
                          const std::string& clientId,
                          const std::string& clientSecret,
                          const std::string& refreshToken);
    std::optional<Calendar> getCalendar(int calendarId) const;
    std::vector<Calendar> listCalendars() const;

    // --- Gestion de eventos ---
    int  addEvent(int calendarId,
                  const std::string& title,
                  const std::string& description,
                  const std::string& location,
                  std::chrono::system_clock::time_point start,
                  std::chrono::system_clock::time_point end,
                  bool isAllDay = false,
                  const RecurrenceRule& recurrence = {});
    bool updateEvent(int eventId,
                     const std::string& title,
                     const std::string& description,
                     const std::string& location,
                     std::chrono::system_clock::time_point start,
                     std::chrono::system_clock::time_point end);
    bool removeEvent(int eventId);
    bool cancelEvent(int eventId);
    std::optional<CalendarEvent> getEvent(int eventId) const;
    std::vector<CalendarEvent> getEvents(int calendarId,
                                          std::chrono::system_clock::time_point from,
                                          std::chrono::system_clock::time_point to) const;
    std::vector<CalendarEvent> getEventsForToday(int calendarId) const;
    std::vector<CalendarEvent> getEventsForWeek(int calendarId) const;
    std::vector<CalendarEvent> searchEvents(const std::string& query) const;

    // --- Attendees ---
    bool addAttendee(int eventId, const std::string& email);
    bool removeAttendee(int eventId, const std::string& email);

    // --- Sincronizacion ---
    SyncResult sync(int calendarId);
    SyncResult syncGoogle(int calendarId);
    SyncResult syncOutlook(int calendarId);
    bool enableAutoSync(int calendarId, std::chrono::seconds interval);
    bool disableAutoSync(int calendarId);

    // --- iCalendar (RFC 5545) ---
    std::string exportToIcs(int calendarId) const;
    std::string eventToIcs(const CalendarEvent& event) const;
    std::vector<CalendarEvent> importFromIcs(const std::string& icsData, int calendarId);

    // --- Callbacks ---
    void onEventAdded(EventCallback callback);
    void onEventUpdated(EventCallback callback);
    void onEventRemoved(EventCallback callback);
    void onSyncCompleted(SyncCallback callback);

    // --- Utilidades ---
    static std::string formatToRfc3339(std::chrono::system_clock::time_point tp);
    static std::chrono::system_clock::time_point parseFromRfc3339(const std::string& str);
    static std::string formatToIcsTimestamp(std::chrono::system_clock::time_point tp);

private:
    mutable std::mutex calendarsMutex_;
    mutable std::mutex eventsMutex_;
    std::map<int, Calendar> calendars_;
    std::map<int, CalendarEvent> events_;

    std::atomic<int> nextCalendarId_{1};
    std::atomic<int> nextEventId_{1};

    // Google Calendar config
    std::map<int, std::string> googleApiKeys_;
    std::map<int, std::string> googleCalendarIds_;

    // Outlook config
    std::map<int, std::string> outlookClientIds_;
    std::map<int, std::string> outlookClientSecrets_;
    std::map<int, std::string> outlookRefreshTokens_;

    // Auto-sync threads
    std::map<int, std::thread> syncThreads_;
    std::map<int, std::atomic<bool>> syncThreadFlags_;
    std::mutex syncThreadsMutex_;

    // Callbacks
    mutable std::mutex callbacksMutex_;
    EventCallback eventAddedCallback_;
    EventCallback eventUpdatedCallback_;
    EventCallback eventRemovedCallback_;
    SyncCallback syncCompletedCallback_;

    // --- Metodos internos ---
    bool calendarExists(int calendarId) const;
    bool eventExists(int eventId) const;
    void notifyEventAdded(const CalendarEvent& event);
    void notifyEventUpdated(const CalendarEvent& event);
    void notifyEventRemoved(const CalendarEvent& event);
    void notifySyncCompleted(int calendarId, const SyncResult& result);

    // --- Generacion ICS ---
    std::string generateIcsHeader() const;
    std::string generateIcsFooter() const;
    std::string generateIcsEvent(const CalendarEvent& event) const;
    std::string recurrenceToIcs(const RecurrenceRule& rr) const;

    // --- Parseo ICS ---
    std::vector<CalendarEvent> parseIcsData(const std::string& icsData, int calendarId);
    CalendarEvent parseIcsEventBlock(const std::string& block, int calendarId);
    std::string extractIcsProperty(const std::string& block, const std::string& prop);
    std::chrono::system_clock::time_point parseIcsTimestamp(const std::string& str);

    // --- Google Calendar API ---
    SyncResult fetchGoogleEvents(int calendarId, const std::string& apiKey,
                                  const std::string& googleCalId);

    // --- Outlook API ---
    std::string refreshOutlookToken(int calendarId);
    SyncResult fetchOutlookEvents(int calendarId, const std::string& accessToken);

    // Auto-sync loop
    void autoSyncLoop(int calendarId, std::chrono::seconds interval);
};

} // namespace powsys365::xtalk
