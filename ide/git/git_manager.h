#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QMutex>
#include <memory>

namespace powsys365::ide::git {

/**
 * @brief Git branch information
 */
struct BranchInfo {
    QString name;
    bool isCurrent = false;
    bool isRemote = false;
    QString remoteName;
    QString tracking;
    QString upstream;
    int ahead = 0;
    int behind = 0;
};

/**
 * @brief Git commit log entry
 */
struct CommitInfo {
    QString hash;
    QString shortHash;
    QString author;
    QString email;
    QDateTime date;
    QString message;
    QString subject;
    QStringList parents;
};

/**
 * @brief Git status entry for a file
 */
struct StatusEntry {
    QString path;
    QString originalPath; // For renamed files
    char indexStatus = ' ';
    char worktreeStatus = ' ';
    bool isStaged = false;
    bool isUnmerged = false;
    bool isIgnored = false;
    bool isUntracked = false;
};

/**
 * @brief Git diff hunk
 */
struct DiffHunk {
    QString header;
    QStringList lines; // prefixed with '+' or '-'
    int oldStart = 0;
    int oldLines = 0;
    int newStart = 0;
    int newLines = 0;
};

/**
 * @brief Git diff for a file
 */
struct FileDiff {
    QString oldPath;
    QString newPath;
    QString oldMode;
    QString newMode;
    bool isNew = false;
    bool isDeleted = false;
    bool isRenamed = false;
    QList<DiffHunk> hunks;
};

/**
 * @brief Repository information
 */
struct RepositoryInfo {
    QString path;
    bool isBare = false;
    QString currentBranch;
    QString remoteUrl;
    int totalCommits = 0;
    int unstagedChanges = 0;
    int stagedChanges = 0;
    int untrackedFiles = 0;
};

/**
 * @brief Git merge/cherry-pick/revert status
 */
enum class MergeState {
    None,
    Merging,
    CherryPicking,
    Reverting,
    Rebasing,
    BISECTing
};

/**
 * @brief Comprehensive Git manager for IDE integration
 */
class GitManager : public QObject {
    Q_OBJECT

public:
    explicit GitManager(QObject* parent = nullptr);
    ~GitManager();

    // === Repository Setup ===
    bool init(const QString& path);
    bool clone(const QString& url, const QString& localPath, const QString& branch = QString());
    bool openRepository(const QString& path);
    bool isRepository() const;
    bool isRepository(const QString& path) const;
    QString repositoryPath() const;
    RepositoryInfo repositoryInfo() const;

    // === Remote Operations ===
    bool fetch(const QString& remote = QString());
    bool pull(const QString& remote = QString(), const QString& branch = QString());
    bool push(const QString& remote = QString(), const QString& branch = QString());
    bool pushTags(const QString& remote = QString());

    // === Commit Operations ===
    bool stage(const QString& file);
    bool stageAll();
    bool unstage(const QString& file);
    bool unstageAll();
    bool commit(const QString& message);
    bool commit(const QString& message, const QString& authorName, const QString& authorEmail);
    bool amend(const QString& message = QString());
    bool revertCommit(const QString& hash);
    bool cherryPick(const QString& hash);

    // === Branch Operations ===
    QList<BranchInfo> branches(bool includeRemote = false) const;
    bool branch(const QString& name);
    bool checkout(const QString& name);
    bool checkoutNewBranch(const QString& name, const QString& startPoint = QString());
    bool merge(const QString& branchName, bool noFastForward = false);
    bool rebase(const QString& branchName);
    bool deleteBranch(const QString& name, bool force = false);
    bool deleteRemoteBranch(const QString& remote, const QString& branch);
    bool renameBranch(const QString& oldName, const QString& newName);

    // === Stash Operations ===
    bool stash(const QString& message = QString());
    bool stashPop();
    bool stashApply(int index = 0);
    bool stashDrop(int index = 0);
    bool stashClear();
    QStringList stashList() const;

    // === Tag Operations ===
    bool tag(const QString& name, const QString& message = QString());
    bool tagDelete(const QString& name);
    bool pushTag(const QString& name, const QString& remote = QString());
    QStringList tags() const;

    // === Status and Log ===
    QList<StatusEntry> status() const;
    QList<CommitInfo> log(int count = 50, const QString& branch = QString()) const;
    QList<FileDiff> diff(const QString& from, const QString& to) const;
    QList<FileDiff> diffUnstaged() const;
    QList<FileDiff> diffStaged() const;
    QList<FileDiff> diffFile(const QString& file) const;
    QString show(const QString& hash, const QString& file = QString()) const;

    // === Information ===
    CommitInfo getCommit(const QString& hash) const;
    QString blame(const QString& file, int line = 0) const;
    QString currentBranch() const;
    MergeState mergeState() const;

    // === Configuration ===
    bool setConfig(const QString& key, const QString& value, bool global = false);
    QString getConfig(const QString& key, bool global = false) const;

    // === Reset Operations ===
    bool resetSoft(const QString& hash);
    bool resetMixed(const QString& hash);
    bool resetHard(const QString& hash);
    bool clean(const QString& path = QString());
    bool cleanAll();

    // === Submodule Operations ===
    bool submoduleAdd(const QString& url, const QString& path);
    bool submoduleUpdate(bool init = false);
    bool submoduleSync();

    // === Signals ===
Q_SIGNALS:
    void repositoryOpened(const QString& path);
    void operationCompleted(const QString& operation, bool success, const QString& output);
    void error(const QString& message);
    void statusChanged();
    void branchChanged(const QString& newBranch);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace powsys365::ide::git
