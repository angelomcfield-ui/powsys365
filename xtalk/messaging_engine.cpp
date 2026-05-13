#include "messaging_engine.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <curl/curl.h>

namespace powsys365::xtalk {

// ============================================================================
// Utilidades
// ============================================================================

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    data->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

std::pair<int, int> XTalkMessagingEngine::makeUserPair(int a, int b) {
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

bool XTalkMessagingEngine::userExists(int userId) const {
    std::shared_lock<std::shared_mutex> lock(usersMutex_);
    return users_.find(userId) != users_.end();
}

bool XTalkMessagingEngine::channelExists(int channelId) const {
    std::shared_lock<std::shared_mutex> lock(channelsMutex_);
    return channels_.find(channelId) != channels_.end();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

XTalkMessagingEngine::XTalkMessagingEngine() {}

XTalkMessagingEngine::~XTalkMessagingEngine() = default;

// ============================================================================
// Gestion de usuarios
// ============================================================================

int XTalkMessagingEngine::registerUser(const std::string& username,
                                       const std::string& displayName,
                                       const std::string& email) {
    std::unique_lock<std::shared_mutex> lock(usersMutex_);
    int uid = nextUserId_.fetch_add(1);
    User user;
    user.userId = uid;
    user.username = username;
    user.displayName = displayName;
    user.email = email;
    user.presence = UserPresence::OFFLINE;
    user.lastActive = std::chrono::system_clock::now();
    users_[uid] = user;
    return uid;
}

bool XTalkMessagingEngine::updateUserPresence(int userId, UserPresence presence) {
    std::unique_lock<std::shared_mutex> lock(usersMutex_);
    auto it = users_.find(userId);
    if (it == users_.end()) return false;
    it->second.presence = presence;
    it->second.lastActive = std::chrono::system_clock::now();
    lock.unlock();
    notifyPresenceChange(userId, presence);
    return true;
}

bool XTalkMessagingEngine::removeUser(int userId) {
    std::unique_lock<std::shared_mutex> lock(usersMutex_);
    return users_.erase(userId) > 0;
}

std::optional<User> XTalkMessagingEngine::getUser(int userId) const {
    std::shared_lock<std::shared_mutex> lock(usersMutex_);
    auto it = users_.find(userId);
    if (it == users_.end()) return std::nullopt;
    return it->second;
}

std::vector<User> XTalkMessagingEngine::getOnlineUsers() const {
    std::shared_lock<std::shared_mutex> lock(usersMutex_);
    std::vector<User> online;
    for (const auto& [id, user] : users_) {
        if (user.presence == UserPresence::ONLINE ||
            user.presence == UserPresence::AWAY) {
            online.push_back(user);
        }
    }
    return online;
}

// ============================================================================
// Gestion de canales
// ============================================================================

int XTalkMessagingEngine::createChannel(const std::string& name,
                                        ChannelType type,
                                        const std::string& description,
                                        int createdByUserId) {
    if (!userExists(createdByUserId)) return -1;

    std::unique_lock<std::shared_mutex> lock(channelsMutex_);
    int cid = nextChannelId_.fetch_add(1);
    Channel ch;
    ch.channelId = cid;
    ch.name = name;
    ch.type = type;
    ch.description = description;
    ch.createdBy = createdByUserId;
    ch.createdAt = std::chrono::system_clock::now();
    ch.members.insert(createdByUserId);
    ch.moderators.insert(createdByUserId);
    ch.isArchived = false;
    channels_[cid] = ch;
    return cid;
}

bool XTalkMessagingEngine::archiveChannel(int channelId) {
    std::unique_lock<std::shared_mutex> lock(channelsMutex_);
    auto it = channels_.find(channelId);
    if (it == channels_.end()) return false;
    it->second.isArchived = true;
    return true;
}

bool XTalkMessagingEngine::addUserToChannel(int channelId, int userId) {
    if (!userExists(userId)) return false;
    std::unique_lock<std::shared_mutex> lock(channelsMutex_);
    auto it = channels_.find(channelId);
    if (it == channels_.end()) return false;
    it->second.members.insert(userId);
    return true;
}

bool XTalkMessagingEngine::removeUserFromChannel(int channelId, int userId) {
    std::unique_lock<std::shared_mutex> lock(channelsMutex_);
    auto it = channels_.find(channelId);
    if (it == channels_.end()) return false;
    it->second.members.erase(userId);
    it->second.moderators.erase(userId);
    return true;
}

bool XTalkMessagingEngine::setChannelTopic(int channelId, const std::string& topic) {
    std::unique_lock<std::shared_mutex> lock(channelsMutex_);
    auto it = channels_.find(channelId);
    if (it == channels_.end()) return false;
    it->second.topic = topic;
    return true;
}

std::optional<Channel> XTalkMessagingEngine::getChannel(int channelId) const {
    std::shared_lock<std::shared_mutex> lock(channelsMutex_);
    auto it = channels_.find(channelId);
    if (it == channels_.end()) return std::nullopt;
    return it->second;
}

std::vector<Channel> XTalkMessagingEngine::getChannelsForUser(int userId) const {
    std::shared_lock<std::shared_mutex> lock(channelsMutex_);
    std::vector<Channel> result;
    for (const auto& [id, ch] : channels_) {
        if (ch.members.count(userId) > 0 && !ch.isArchived) {
            result.push_back(ch);
        }
    }
    return result;
}

// ============================================================================
// Mensajes Directos (DM)
// ============================================================================

int XTalkMessagingEngine::createDM(int userId1, int userId2) {
    if (userId1 == userId2) return -1;
    if (!userExists(userId1) || !userExists(userId2)) return -1;

    auto pair = makeUserPair(userId1, userId2);

    {
        std::shared_lock<std::shared_mutex> lock(channelsMutex_);
        auto it = dmChannels_.find(pair);
        if (it != dmChannels_.end()) return it->second;
    }

    std::unique_lock<std::shared_mutex> lock(channelsMutex_);
    // Double-check
    auto it = dmChannels_.find(pair);
    if (it != dmChannels_.end()) return it->second;

    int cid = nextChannelId_.fetch_add(1);
    Channel ch;
    ch.channelId = cid;
    ch.name = "DM_" + std::to_string(userId1) + "_" + std::to_string(userId2);
    ch.type = ChannelType::DIRECT_MESSAGE;
    ch.description = "Direct message";
    ch.createdBy = userId1;
    ch.createdAt = std::chrono::system_clock::now();
    ch.members.insert(userId1);
    ch.members.insert(userId2);
    ch.isArchived = false;
    channels_[cid] = ch;
    dmChannels_[pair] = cid;
    return cid;
}

std::optional<int> XTalkMessagingEngine::findDM(int userId1, int userId2) const {
    if (userId1 == userId2) return std::nullopt;
    std::shared_lock<std::shared_mutex> lock(channelsMutex_);
    auto it = dmChannels_.find(makeUserPair(userId1, userId2));
    if (it != dmChannels_.end()) return it->second;
    return std::nullopt;
}

// ============================================================================
// Envio de mensajes
// ============================================================================

int XTalkMessagingEngine::sendMessage(int channelId, int senderId,
                                      const std::string& content,
                                      MessageType type) {
    if (!userExists(senderId)) return -1;
    if (!channelExists(channelId)) return -1;

    // Verificar que el remitente es miembro del canal
    {
        std::shared_lock<std::shared_mutex> lock(channelsMutex_);
        auto chIt = channels_.find(channelId);
        if (chIt == channels_.end() || chIt->second.members.count(senderId) == 0) {
            return -1;
        }
    }

    int mid = nextMessageId_.fetch_add(1);
    Message msg;
    msg.messageId = mid;
    msg.senderId = senderId;
    msg.channelId = channelId;
    msg.threadParentId = 0;
    msg.content = content;
    msg.type = type;
    msg.timestamp = std::chrono::system_clock::now();
    msg.isDeleted = false;

    {
        std::unique_lock<std::shared_mutex> lock(messagesMutex_);
        messages_[mid] = msg;
    }

    notifyNewMessage(msg);
    return mid;
}

bool XTalkMessagingEngine::editMessage(int messageId, const std::string& newContent) {
    std::unique_lock<std::shared_mutex> lock(messagesMutex_);
    auto it = messages_.find(messageId);
    if (it == messages_.end() || it->second.isDeleted) return false;
    it->second.content = newContent;
    it->second.editedAt = std::chrono::system_clock::now();
    return true;
}

bool XTalkMessagingEngine::deleteMessage(int messageId) {
    std::unique_lock<std::shared_mutex> lock(messagesMutex_);
    auto it = messages_.find(messageId);
    if (it == messages_.end()) return false;
    it->second.isDeleted = true;
    it->second.content = "[Mensaje eliminado]";
    return true;
}

// ============================================================================
// Hilos (Threads)
// ============================================================================

int XTalkMessagingEngine::replyInThread(int parentMessageId, int senderId,
                                        const std::string& content) {
    if (!userExists(senderId)) return -1;

    std::shared_lock<std::shared_mutex> msgLock(messagesMutex_);
    auto parentIt = messages_.find(parentMessageId);
    if (parentIt == messages_.end() || parentIt->second.isDeleted) return -1;
    int channelId = parentIt->second.channelId;
    msgLock.unlock();

    // Enviar como mensaje normal con referencia al padre
    int mid = nextMessageId_.fetch_add(1);
    Message msg;
    msg.messageId = mid;
    msg.senderId = senderId;
    msg.channelId = channelId;
    msg.threadParentId = parentMessageId;
    msg.content = content;
    msg.type = MessageType::TEXT;
    msg.timestamp = std::chrono::system_clock::now();
    msg.isDeleted = false;

    {
        std::unique_lock<std::shared_mutex> lock(messagesMutex_);
        messages_[mid] = msg;
    }

    // Actualizar o crear el thread
    {
        std::unique_lock<std::shared_mutex> lock(threadsMutex_);
        auto& thread = threads_[parentMessageId];
        thread.threadId = parentMessageId;
        thread.parentMessageId = parentMessageId;
        thread.channelId = channelId;
        thread.messageIds.push_back(mid);
        thread.lastReplyAt = msg.timestamp;

        // Contar participantes unicos
        std::set<int> participants;
        for (int midInThread : thread.messageIds) {
            auto mIt = messages_.find(midInThread);
            if (mIt != messages_.end()) {
                participants.insert(mIt->second.senderId);
            }
        }
        thread.participantCount = static_cast<int>(participants.size());
    }

    notifyNewMessage(msg);
    return mid;
}

std::vector<Message> XTalkMessagingEngine::getThreadMessages(int parentMessageId) const {
    std::shared_lock<std::shared_mutex> lock(threadsMutex_);
    auto it = threads_.find(parentMessageId);
    if (it == threads_.end()) return {};

    std::vector<Message> result;
    std::shared_lock<std::shared_mutex> msgLock(messagesMutex_);
    for (int mid : it->second.messageIds) {
        auto mIt = messages_.find(mid);
        if (mIt != messages_.end() && !mIt->second.isDeleted) {
            result.push_back(mIt->second);
        }
    }
    return result;
}

// ============================================================================
// Reacciones
// ============================================================================

bool XTalkMessagingEngine::addReaction(int messageId, int userId,
                                       const std::string& emoji) {
    std::unique_lock<std::shared_mutex> lock(messagesMutex_);
    auto it = messages_.find(messageId);
    if (it == messages_.end() || it->second.isDeleted) return false;

    // Buscar reaccion existente
    for (auto& reaction : it->second.reactions) {
        if (reaction.emoji == emoji) {
            reaction.userIds.push_back(userId);
            reaction.timestamp = std::chrono::system_clock::now();
            return true;
        }
    }

    // Crear nueva reaccion
    Reaction r;
    r.emoji = emoji;
    r.userIds.push_back(userId);
    r.timestamp = std::chrono::system_clock::now();
    it->second.reactions.push_back(r);
    return true;
}

bool XTalkMessagingEngine::removeReaction(int messageId, int userId,
                                          const std::string& emoji) {
    std::unique_lock<std::shared_mutex> lock(messagesMutex_);
    auto it = messages_.find(messageId);
    if (it == messages_.end()) return false;

    for (auto& reaction : it->second.reactions) {
        if (reaction.emoji == emoji) {
            auto uit = std::find(reaction.userIds.begin(), reaction.userIds.end(), userId);
            if (uit != reaction.userIds.end()) {
                reaction.userIds.erase(uit);
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// Consultas
// ============================================================================

std::vector<Message> XTalkMessagingEngine::getChannelMessages(int channelId,
                                                               int limit,
                                                               int offset) const {
    std::shared_lock<std::shared_mutex> lock(messagesMutex_);
    std::vector<Message> result;
    for (const auto& [id, msg] : messages_) {
        if (msg.channelId == channelId && !msg.isDeleted) {
            result.push_back(msg);
        }
    }
    // Ordenar por timestamp descendente
    std::sort(result.begin(), result.end(),
        [](const Message& a, const Message& b) {
            return a.timestamp > b.timestamp;
        });

    // Aplicar offset y limit
    if (offset >= static_cast<int>(result.size())) return {};
    size_t start = offset;
    size_t end = std::min(start + static_cast<size_t>(limit), result.size());
    std::vector<Message> paginated(result.begin() + start, result.begin() + end);

    // Revertir a orden ascendente
    std::reverse(paginated.begin(), paginated.end());
    return paginated;
}

std::vector<Message> XTalkMessagingEngine::searchMessages(const std::string& query,
                                                           int channelId) const {
    std::shared_lock<std::shared_mutex> lock(messagesMutex_);
    std::vector<Message> result;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    for (const auto& [id, msg] : messages_) {
        if (msg.isDeleted) continue;
        if (channelId != 0 && msg.channelId != channelId) continue;

        std::string lowerContent = msg.content;
        std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(), ::tolower);
        if (lowerContent.find(lowerQuery) != std::string::npos) {
            result.push_back(msg);
        }
    }
    return result;
}

// ============================================================================
// Callbacks
// ============================================================================

void XTalkMessagingEngine::onNewMessage(MessageCallback callback) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    messageCallbacks_.push_back(callback);
}

void XTalkMessagingEngine::onPresenceChange(PresenceCallback callback) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    presenceCallbacks_.push_back(callback);
}

void XTalkMessagingEngine::onTyping(TypingCallback callback) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    typingCallbacks_.push_back(callback);
}

void XTalkMessagingEngine::notifyNewMessage(const Message& msg) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    for (auto& cb : messageCallbacks_) {
        try { cb(msg); } catch (...) {}
    }
}

void XTalkMessagingEngine::notifyPresenceChange(int userId, UserPresence presence) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    for (auto& cb : presenceCallbacks_) {
        try { cb(userId, presence); } catch (...) {}
    }
}

void XTalkMessagingEngine::notifyTyping(int channelId, int userId) {
    std::unique_lock<std::mutex> lock(callbacksMutex_);
    for (auto& cb : typingCallbacks_) {
        try { cb(channelId, userId); } catch (...) {}
    }
}

void XTalkMessagingEngine::sendTypingIndicator(int channelId, int userId) {
    notifyTyping(channelId, userId);
}

// ============================================================================
// Integraciones
// ============================================================================

bool XTalkMessagingEngine::configureSlackWebhook(const std::string& webhookUrl,
                                                  const std::string& channelName) {
    std::unique_lock<std::mutex> lock(integrationMutex_);
    slackWebhookUrl_ = webhookUrl;
    slackChannelName_ = channelName;
    return true;
}

bool XTalkMessagingEngine::configureWhatsAppBusiness(const std::string& apiKey,
                                                      const std::string& phoneNumberId) {
    std::unique_lock<std::mutex> lock(integrationMutex_);
    whatsappApiKey_ = apiKey;
    whatsappPhoneNumberId_ = phoneNumberId;
    return true;
}

bool XTalkMessagingEngine::sendToExternal(int channelId, const std::string& platform,
                                          const std::string& message) {
    std::unique_lock<std::mutex> lock(integrationMutex_);

    if (platform == "slack" && !slackWebhookUrl_.empty()) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string jsonPayload = "{\"text\":\"" + message + "\",\"channel\":\""
                                  + slackChannelName_ + "\"}";
        std::string response;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, slackWebhookUrl_.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return res == CURLE_OK;
    }

    if (platform == "whatsapp" && !whatsappApiKey_.empty()) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string url = "https://graph.facebook.com/v18.0/" + whatsappPhoneNumberId_ + "/messages";
        std::string jsonPayload = "{\"messaging_product\":\"whatsapp\",\"type\":\"text\","
                                  "\"to\":\"default\",\"text\":{\"body\":\"" + message + "\"}}";
        std::string response;
        std::string authHeader = "Authorization: Bearer " + whatsappApiKey_;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, authHeader.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return res == CURLE_OK;
    }

    return false;
}

// ============================================================================
// Persistencia (formato CSV simple)
// ============================================================================

bool XTalkMessagingEngine::saveToDisk(const std::string& path) const {
    std::ofstream file(path + "/messages.csv");
    if (!file.is_open()) return false;

    file << "messageId,senderId,channelId,threadParentId,content,timestamp,isDeleted\n";
    std::shared_lock<std::shared_mutex> lock(messagesMutex_);
    for (const auto& [id, msg] : messages_) {
        file << msg.messageId << ","
             << msg.senderId << ","
             << msg.channelId << ","
             << msg.threadParentId << ","
             << "\"" << msg.content << "\","
             << std::chrono::duration_cast<std::chrono::seconds>(
                   msg.timestamp.time_since_epoch()).count() << ","
             << (msg.isDeleted ? "1" : "0") << "\n";
    }
    return true;
}

bool XTalkMessagingEngine::loadFromDisk(const std::string& path) {
    std::ifstream file(path + "/messages.csv");
    if (!file.is_open()) return false;

    std::string line;
    std::getline(file, line); // header

    std::unique_lock<std::shared_mutex> lock(messagesMutex_);
    messages_.clear();

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        Message msg;

        std::getline(ss, token, ','); msg.messageId = std::stoi(token);
        std::getline(ss, token, ','); msg.senderId = std::stoi(token);
        std::getline(ss, token, ','); msg.channelId = std::stoi(token);
        std::getline(ss, token, ','); msg.threadParentId = std::stoi(token);
        // Content between quotes
        std::getline(ss, token, '"');
        std::getline(ss, token, '"');
        msg.content = token;
        std::getline(ss, token, ',');
        std::getline(ss, token, ',');
        auto ts = std::stoll(token);
        msg.timestamp = std::chrono::system_clock::time_point(std::chrono::seconds(ts));
        std::getline(ss, token, ','); msg.isDeleted = (token == "1");
        messages_[msg.messageId] = msg;
    }
    return true;
}

// ============================================================================
// Estadisticas
// ============================================================================

size_t XTalkMessagingEngine::getTotalMessages() const {
    std::shared_lock<std::shared_mutex> lock(messagesMutex_);
    return messages_.size();
}

size_t XTalkMessagingEngine::getActiveChannelCount() const {
    std::shared_lock<std::shared_mutex> lock(channelsMutex_);
    size_t count = 0;
    for (const auto& [id, ch] : channels_) {
        if (!ch.isArchived) ++count;
    }
    return count;
}

size_t XTalkMessagingEngine::getOnlineUserCount() const {
    std::shared_lock<std::shared_mutex> lock(usersMutex_);
    size_t count = 0;
    for (const auto& [id, user] : users_) {
        if (user.presence == UserPresence::ONLINE) ++count;
    }
    return count;
}

} // namespace powsys365::xtalk
