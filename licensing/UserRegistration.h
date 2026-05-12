#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>

namespace powsys365 {
namespace licensing {

// ============================================================
// User - Domain model for registered users
// ============================================================
struct User {
    std::string id;                    // UUID
    std::string email;
    std::string passwordHash;          // bcrypt hash
    std::string fullName;
    std::string company;
    std::string phone;
    std::string licenseKey;            // FK to licenses
    bool isActive = true;
    bool isVerified = false;
    std::string verificationToken;     // JWT or random token
    std::string resetToken;            // Password reset token
    std::chrono::system_clock::time_point resetTokenExpires;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::chrono::system_clock::time_point lastLogin;
};

// ============================================================
// RegistrationResult
// ============================================================
struct RegistrationResult {
    bool success = false;
    std::string userId;
    std::string errorCode;
    std::string errorMessage;
    std::string verificationToken; // Token to send via email
};

// ============================================================
// AuthenticationResult
// ============================================================
struct AuthenticationResult {
    bool success = false;
    std::string userId;
    std::string email;
    std::string licenseKey;
    std::string errorCode;
    std::string errorMessage;
    std::chrono::system_clock::time_point authenticatedAt;
};

// ============================================================
// PasswordValidationResult
// ============================================================
struct PasswordValidationResult {
    bool valid = false;
    std::vector<std::string> errors;

    bool isValid() const { return valid && errors.empty(); }
};

// ============================================================
// UserRegistration - User management with bcrypt
// ============================================================
class UserRegistration {
public:
    UserRegistration();
    ~UserRegistration();

    // No copy
    UserRegistration(const UserRegistration&) = delete;
    UserRegistration& operator=(const UserRegistration&) = delete;

    // --- Registration ---
    RegistrationResult registerUser(const std::string& email,
                                     const std::string& password,
                                     const std::string& fullName = "",
                                     const std::string& company = "",
                                     const std::string& phone = "");

    // --- Authentication ---
    AuthenticationResult authenticate(const std::string& email,
                                       const std::string& password);

    // --- Password Management ---
    bool changePassword(const std::string& userId,
                        const std::string& currentPassword,
                        const std::string& newPassword);

    std::string generateResetToken(const std::string& email);
    bool resetPassword(const std::string& resetToken,
                       const std::string& newPassword);

    // --- Email Verification ---
    std::string generateVerificationToken(const std::string& userId);
    bool verifyEmail(const std::string& token);

    // --- User Queries ---
    std::optional<User> findByEmail(const std::string& email) const;
    std::optional<User> findById(const std::string& userId) const;
    std::vector<User> listUsers() const;
    bool deactivateUser(const std::string& userId);
    bool reactivateUser(const std::string& userId);
    bool assignLicense(const std::string& userId, const std::string& licenseKey);

    // --- Password Validation ---
    static PasswordValidationResult validatePassword(const std::string& password);
    static bool validateEmailFormat(const std::string& email);

    // --- Callbacks ---
    using EmailSenderCallback = std::function<bool(const std::string& toEmail,
                                                    const std::string& subject,
                                                    const std::string& body)>;
    void setEmailSender(EmailSenderCallback sender);

private:
    mutable std::mutex m_mutex;
    std::vector<User> m_users;  // In-memory store (replace with DB in production)
    EmailSenderCallback m_emailSender;

    // bcrypt helpers (wrapping bcrypt C library)
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& hash);

    // Token generation
    static std::string generateSecureToken(size_t length = 32);
    static std::string generateUuid();

    bool sendVerificationEmailInternal(const User& user, const std::string& token);
    bool sendPasswordResetEmailInternal(const User& user, const std::string& token);
};

} // namespace licensing
} // namespace powsys365
