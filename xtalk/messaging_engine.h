#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>
#include <optional>
#include <unordered_map>
#include <condition_variable>
#include <queue>

namespace powsys365::xtalk {

// ============================================================================
// Tipos de mensaje
// ============================================================================

enum class MessageType {
    TEXT,
    FILE_ATTACHMENT,
    SYSTEM_NOTIFICATION,
    INTEGRATION_WEBHOOK
};

enum class ChannelType {
    PUBLIC,
    PRIVATE,
    DIRECT_MESSAGE
};

enum class UserPresence {
    ONLINE,
    AWAY,
    DO_NOT_DISTURB,
    OFFLINE
};

// ============================================================================
// Estructuras de datos
// ============================================================================

struct User {
    int    userId;
    std::string username;
    std::string displayName;
    std::string email;
    std::string avatarUrl;
    UserPresence presence;
    std::chrono::system_clock::time_point lastActive;
};

struct Reaction {
    std::string emoji;  // unicode o shortcode
    std::vector<int> userIds;
    std::chrono::system_clock::time_point timestamp;
};

struct Message {
    int    messageId;
    int    senderId;
    int    channelId;
    int    threadParentId;   // 0 si es mensaje raiz
    std::string content;
    MessageType type;
    std::chrono::system_clock::time_point timestamp;
    std::chrono::system_clock::time_point editedAt;
    bool   isDeleted;
    std::vector<Reaction> reactions;
    std::vector<std::string> attachmentUrls;
};

struct Channel {
    int    channelId;
    std::string name;
    ChannelType type;
    std::string description;
    int    createdBy;
    std::chrono::system_clock::time_point createdAt;
    std::set<int> members;
    std::set<int> moderators;
    std::string topic;
    bool   isArchived;
};

struct Thread {
    int    threadId;
    int    parentMessageId;
    int    channelId;
    std::vector<int> messageIds;
    int    participantCount;
    std::chrono::system_clock::time_point lastReplyAt;
};

// ============================================================================
// Callbacks
// ============================================================================

using MessageCallback = std::function<void(const Message&)>;
using PresenceCallback = std::function<void(int userId, UserPresence)>;
using TypingCallback = std::function<void(int channelId, int userId)>;

// ============================================================================
// XTalkMessagingEngine
// ============================================================================

class XTalkMessagingEngine {
public:
    XTalkMessagingEngine();
    ~XTalkMessagingEngine();

    // --- Gestion de usuarios ---
    int  registerUser(const std::string& username,
                     const std::string& displayName,
                     const std::string& email);
    bool updateUserPresence(int userId, UserPresence presence);
    bool removeUser(int userId);
    std::optional<User> getUser(int userId) const;
    std::vector<User> getOnlineUsers() const;

    // --- Gestion de canales ---
    int  createChannel(const std::string& name,
                      ChannelType type,
                      const std::string& description,
                      int createdByUserId);
    bool archiveChannel(int channelId);
    bool addUserToChannel(int channelId, int userId);
    bool removeUserFromChannel(int channelId, int userId);
    bool setChannelTopic(int channelId, const std::string& topic);
    std::optional<Channel> getChannel(int channelId) const;
    std::vector<Channel> getChannelsForUser(int userId) const;

    // --- Mensajes Directos (DM) ---
    int  createDM(int userId1, int userId2);
    std::optional<int> findDM(int userId1, int userId2) const;

    // --- Envio de mensajes ---
    int  sendMessage(int channelId, int senderId,
                    const std::string& content,
                    MessageType type = MessageType::TEXT);
    bool editMessage(int messageId, const std::string& newContent);
    bool deleteMessage(int messageId);

    // --- Hilos (Threads) ---
    int  replyInThread(int parentMessageId, int senderId,
                      const std::string& content);
    std::vector<Message> getThreadMessages(int parentMessageId) const;

    // --- Reacciones ---
    bool addReaction(int messageId, int userId, const std::string& emoji);
    bool removeReaction(int messageId, int userId, const std::string& emoji);

    // --- Consultas ---
    std::vector<Message> getChannelMessages(int channelId, int limit = 50,
                                            int offset = 0) const;
    std::vector<Message> searchMessages(const std::string& query,
                                        int channelId = 0) const;

    // --- Callbacks ---
    void onNewMessage(MessageCallback callback);
    void onPresenceChange(PresenceCallback callback);
    void onTyping(TypingCallback callback);

    // --- Indicador de escritura ---
    void sendTypingIndicator(int channelId, int userId);

    // --- Integraciones ---
    bool configureSlackWebhook(const std::string& webhookUrl,
                              const std::string& channelName);
    bool configureWhatsAppBusiness(const std::string& apiKey,
                                    const std::string& phoneNumberId);
    bool sendToExternal(int channelId, const std::string& platform,
                       const std::string& message);

    // --- Persistencia ---
    bool saveToDisk(const std::string& path) const;
    bool loadFromDisk(const std::string& path);

    // --- Estadisticas ---
    size_t getTotalMessages() const;
    size_t getActiveChannelCount() const;
    size_t getOnlineUserCount() const;

private:
    mutable std::shared_mutex usersMutex_;
    mutable std::shared_mutex channelsMutex_;
    mutable std::shared_mutex messagesMutex_;
    mutable std::shared_mutex threadsMutex_;

    std::unordered_map<int, User> users_;
    std::unordered_map<int, Channel> channels_;
    std::unordered_map<int, Message> messages_;
    std::unordered_map<int, Thread> threads_;

    // Mapa de pares de usuarios a canal DM
    std::map<std::pair<int, int>, int> dmChannels_;

    std::atomic<int> nextUserId_{1};
    std::atomic<int> nextChannelId_{1};
    std::atomic<int> nextMessageId_{1};
    std::atomic<int> nextThreadId_{1};

    // Callbacks
    std::vector<MessageCallback> messageCallbacks_;
    std::vector<PresenceCallback> presenceCallbacks_;
    std::vector<TypingCallback> typingCallbacks_;
    std::mutex callbacksMutex_;

    // Configuracion de integraciones
    std::string slackWebhookUrl_;
    std::string slackChannelName_;
    std::string whatsappApiKey_;
    std::string whatsappPhoneNumberId_;
    std::mutex integrationMutex_;

    // Notificar callbacks
    void notifyNewMessage(const Message& msg);
    void notifyPresenceChange(int userId, UserPresence presence);
    void notifyTyping(int channelId, int userId);

    // Utilidades
    static std::pair<int, int> makeUserPair(int a, int b);
    bool userExists(int userId) const;
    bool channelExists(int channelId) const;
};

} // namespace powsys365::xtalk
