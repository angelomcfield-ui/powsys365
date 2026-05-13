#include "git_manager.h"
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QMutexLocker>
#include <QDateTime>
#include <QCoreApplication>

namespace powsys365::ide::git {

class GitManager::Impl {
public:
    GitManager* q;
    QString repoPath;
    QString gitExecutable = "git";
    mutable QMutex mutex;

    explicit Impl(GitManager* parent) : q(parent) {}

    QStringList runGit(const QStringList& args, bool* ok = nullptr,
                       int timeoutMs = 30000) const {
        QMutexLocker lock(&mutex);

        QProcess process;
        process.setProgram(gitExecutable);
        QStringList allArgs = args;
        if (!repoPath.isEmpty()) {
            process.setWorkingDirectory(repoPath);
        }
        process.setArguments(allArgs);
        process.start();

        if (!process.waitForFinished(timeoutMs)) {
            if (ok) *ok = false;
            Q_EMIT q->error(QString("Git command timed out: git %1").arg(args.join(" ")));
            return QStringList();
        }

        if (process.exitCode() != 0) {
            if (ok) *ok = false;
            QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (!err.isEmpty()) {
                Q_EMIT q->error(err);
            }
            return QStringList();
        }

        if (ok) *ok = true;
        QByteArray output = process.readAllStandardOutput();
        QStringList lines = QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
        return lines;
    }

    QString runGitStr(const QStringList& args, bool* ok = nullptr,
                      int timeoutMs = 30000) const {
        bool success = false;
        QStringList lines = runGit(args, &success, timeoutMs);
        if (ok) *ok = success;
        return lines.join('\n');
    }
};

GitManager::GitManager(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(this))
{
}

GitManager::~GitManager() = default;

bool GitManager::init(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            Q_EMIT error(QString("Cannot create directory: %1").arg(path));
            return false;
        }
    }

    bool ok = false;
    d->runGit({"init"}, &ok, 30000);
    if (ok) {
        d->repoPath = path;
        Q_EMIT repositoryOpened(path);
        Q_EMIT operationCompleted("init", true, "Repository initialized");
        return true;
    }
    Q_EMIT operationCompleted("init", false, "Failed to initialize repository");
    return false;
}

bool GitManager::clone(const QString& url, const QString& localPath, const QString& branch) {
    QDir parentDir(QFileInfo(localPath).path());
    if (!parentDir.exists()) {
        parentDir.mkpath(".");
    }

    QStringList args = {"clone", "--progress"};
    if (!branch.isEmpty()) {
        args << "--branch" << branch;
    }
    args << url << localPath;

    bool ok = false;
    d->runGit(args, &ok, 300000);
    if (ok) {
        d->repoPath = localPath;
        Q_EMIT repositoryOpened(localPath);
        Q_EMIT operationCompleted("clone", true, "Repository cloned successfully");
        return true;
    }
    Q_EMIT operationCompleted("clone", false, "Failed to clone repository");
    return false;
}

bool GitManager::openRepository(const QString& path) {
    if (isRepository(path)) {
        d->repoPath = path;
        Q_EMIT repositoryOpened(path);
        return true;
    }
    Q_EMIT error(QString("Not a git repository: %1").arg(path));
    return false;
}

bool GitManager::isRepository() const {
    return isRepository(d->repoPath);
}

bool GitManager::isRepository(const QString& path) const {
    if (path.isEmpty()) return false;
    bool ok = false;
    QProcess proc;
    proc.setProgram(d->gitExecutable);
    proc.setWorkingDirectory(path);
    proc.setArguments({"rev-parse", "--git-dir"});
    proc.start();
    if (proc.waitForFinished(5000)) {
        ok = (proc.exitCode() == 0);
    }
    return ok;
}

QString GitManager::repositoryPath() const {
    return d->repoPath;
}

RepositoryInfo GitManager::repositoryInfo() const {
    RepositoryInfo info;
    info.path = d->repoPath;
    if (d->repoPath.isEmpty()) return info;

    bool ok = false;
    QString bareStr = d->runGitStr({"config", "--get", "core.bare"}, &ok);
    info.isBare = (bareStr.trimmed() == "true");

    info.currentBranch = currentBranch();

    QString remoteStr = d->runGitStr({"config", "--get", "remote.origin.url"}, &ok);
    if (ok) info.remoteUrl = remoteStr.trimmed();

    QString countStr = d->runGitStr({"rev-list", "--count", "HEAD"}, &ok);
    if (ok) info.totalCommits = countStr.toInt();

    auto entries = status();
    for (const auto& e : entries) {
        if (e.isStaged) info.stagedChanges++;
        if (e.indexStatus != ' ' || (e.worktreeStatus != ' ' && !e.isUntracked)) info.unstagedChanges++;
        if (e.isUntracked) info.untrackedFiles++;
    }

    return info;
}

// === Remote Operations ===

bool GitManager::fetch(const QString& remote) {
    QStringList args = {"fetch"};
    if (!remote.isEmpty()) args << remote;
    else args << "--all";

    bool ok = false;
    d->runGit(args, &ok, 120000);
    Q_EMIT operationCompleted("fetch", ok, ok ? "Fetch completed" : "Fetch failed");
    return ok;
}

bool GitManager::pull(const QString& remote, const QString& branch) {
    QStringList args = {"pull"};
    if (!remote.isEmpty()) args << remote;
    if (!branch.isEmpty()) args << branch;

    bool ok = false;
    d->runGit(args, &ok, 120000);
    Q_EMIT operationCompleted("pull", ok, ok ? "Pull completed" : "Pull failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::push(const QString& remote, const QString& branch) {
    QStringList args = {"push"};
    if (!remote.isEmpty()) args << remote;
    if (!branch.isEmpty()) args << branch;
    else args << "--all";

    bool ok = false;
    d->runGit(args, &ok, 120000);
    Q_EMIT operationCompleted("push", ok, ok ? "Push completed" : "Push failed");
    return ok;
}

bool GitManager::pushTags(const QString& remote) {
    QStringList args = {"push"};
    if (!remote.isEmpty()) args << remote;
    else args << "origin";
    args << "--tags";

    bool ok = false;
    d->runGit(args, &ok, 60000);
    Q_EMIT operationCompleted("pushTags", ok, ok ? "Tags pushed" : "Push tags failed");
    return ok;
}

// === Commit Operations ===

bool GitManager::stage(const QString& file) {
    bool ok = false;
    d->runGit({"add", file}, &ok);
    Q_EMIT operationCompleted("stage", ok, ok ? "File staged" : "Stage failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::stageAll() {
    bool ok = false;
    d->runGit({"add", "-A"}, &ok);
    Q_EMIT operationCompleted("stageAll", ok, ok ? "All changes staged" : "Stage all failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::unstage(const QString& file) {
    bool ok = false;
    d->runGit({"reset", "HEAD", "--", file}, &ok);
    Q_EMIT operationCompleted("unstage", ok, ok ? "File unstaged" : "Unstage failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::unstageAll() {
    bool ok = false;
    d->runGit({"reset", "HEAD", "--", "."}, &ok);
    Q_EMIT operationCompleted("unstageAll", ok, ok ? "All files unstaged" : "Unstage all failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::commit(const QString& message) {
    bool ok = false;
    d->runGit({"commit", "-m", message}, &ok);
    Q_EMIT operationCompleted("commit", ok, ok ? "Commit successful" : "Commit failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::commit(const QString& message, const QString& authorName,
                         const QString& authorEmail) {
    QString author = QString("%1 <%2>").arg(authorName, authorEmail);
    bool ok = false;
    d->runGit({"commit", "-m", message, "--author", author}, &ok);
    Q_EMIT operationCompleted("commit", ok, ok ? "Commit successful" : "Commit failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::amend(const QString& message) {
    QStringList args = {"commit", "--amend", "--no-edit"};
    if (!message.isEmpty()) {
        args = {"commit", "--amend", "-m", message};
    }
    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("amend", ok, ok ? "Amend successful" : "Amend failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::revertCommit(const QString& hash) {
    bool ok = false;
    d->runGit({"revert", "--no-edit", hash}, &ok);
    Q_EMIT operationCompleted("revert", ok, ok ? "Revert successful" : "Revert failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::cherryPick(const QString& hash) {
    bool ok = false;
    d->runGit({"cherry-pick", hash}, &ok);
    Q_EMIT operationCompleted("cherryPick", ok, ok ? "Cherry-pick successful" : "Cherry-pick failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

// === Branch Operations ===

QList<BranchInfo> GitManager::branches(bool includeRemote) const {
    QList<BranchInfo> result;
    if (d->repoPath.isEmpty()) return result;

    QStringList args = {"branch", "-vvv"};
    if (includeRemote) args << "-a";

    bool ok = false;
    QStringList lines = d->runGit(args, &ok);

    for (const QString& line : lines) {
        if (line.size() < 2) continue;

        BranchInfo bi;
        QString rest = line;

        // Check if current branch (starts with '*')
        if (rest.startsWith("* ")) {
            bi.isCurrent = true;
            rest = rest.mid(2);
        } else if (rest.startsWith("  ")) {
            rest = rest.mid(2);
        }

        // Parse remote branches
        if (rest.startsWith("remotes/")) {
            bi.isRemote = true;
            rest = rest.mid(8);
            int slashIdx = rest.indexOf('/');
            if (slashIdx > 0) {
                bi.remoteName = rest.left(slashIdx);
                bi.name = rest.mid(slashIdx + 1);
            } else {
                bi.name = rest;
            }
            result.append(bi);
            continue;
        }

        // Parse local branch name
        static const QRegularExpression branchRe("^(\\S+)\\s*(.*)");
        QRegularExpressionMatch match = branchRe.match(rest);
        if (match.hasMatch()) {
            bi.name = match.captured(1);
            QString info = match.captured(2).trimmed();

            // Parse tracking info [origin/main: ahead 1, behind 2]
            static const QRegularExpression trackRe("\\[(.+?)\\]");
            QRegularExpressionMatch trackMatch = trackRe.match(info);
            if (trackMatch.hasMatch()) {
                bi.tracking = trackMatch.captured(1);
                QString trackInfo = bi.tracking;

                static const QRegularExpression aheadRe("ahead\\s+(\\d+)");
                static const QRegularExpression behindRe("behind\\s+(\\d+)");
                QRegularExpressionMatch aheadMatch = aheadRe.match(trackInfo);
                QRegularExpressionMatch behindMatch = behindRe.match(trackInfo);
                if (aheadMatch.hasMatch()) bi.ahead = aheadMatch.captured(1).toInt();
                if (behindMatch.hasMatch()) bi.behind = behindMatch.captured(1).toInt();

                int colonIdx = trackInfo.indexOf(':');
                if (colonIdx > 0) {
                    bi.upstream = trackInfo.left(colonIdx).trimmed();
                }
            }
        }

        result.append(bi);
    }

    return result;
}

bool GitManager::branch(const QString& name) {
    bool ok = false;
    d->runGit({"branch", name}, &ok);
    Q_EMIT operationCompleted("branch", ok, ok ? QString("Branch '%1' created").arg(name)
                                                  : "Branch creation failed");
    return ok;
}

bool GitManager::checkout(const QString& name) {
    bool ok = false;
    d->runGit({"checkout", name}, &ok);
    Q_EMIT operationCompleted("checkout", ok, ok ? QString("Checked out '%1'").arg(name)
                                                  : "Checkout failed");
    if (ok) Q_EMIT branchChanged(name);
    return ok;
}

bool GitManager::checkoutNewBranch(const QString& name, const QString& startPoint) {
    QStringList args = {"checkout", "-b", name};
    if (!startPoint.isEmpty()) args << startPoint;
    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("checkoutNewBranch", ok,
        ok ? QString("Branch '%1' created and checked out").arg(name)
           : "Branch creation failed");
    if (ok) Q_EMIT branchChanged(name);
    return ok;
}

bool GitManager::merge(const QString& branchName, bool noFastForward) {
    QStringList args = {"merge"};
    if (noFastForward) args << "--no-ff";
    args << "--no-edit" << branchName;

    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("merge", ok,
        ok ? QString("Branch '%1' merged").arg(branchName) : "Merge failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::rebase(const QString& branchName) {
    bool ok = false;
    d->runGit({"rebase", branchName}, &ok);
    Q_EMIT operationCompleted("rebase", ok,
        ok ? QString("Rebased onto '%1'").arg(branchName) : "Rebase failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::deleteBranch(const QString& name, bool force) {
    QStringList args = {"branch"};
    args << (force ? "-D" : "-d") << name;
    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("deleteBranch", ok,
        ok ? QString("Branch '%1' deleted").arg(name) : "Branch deletion failed");
    return ok;
}

bool GitManager::deleteRemoteBranch(const QString& remote, const QString& branch) {
    bool ok = false;
    d->runGit({"push", remote, "--delete", branch}, &ok);
    Q_EMIT operationCompleted("deleteRemoteBranch", ok,
        ok ? QString("Remote branch '%1/%2' deleted").arg(remote, branch)
           : "Remote branch deletion failed");
    return ok;
}

bool GitManager::renameBranch(const QString& oldName, const QString& newName) {
    bool ok = false;
    d->runGit({"branch", "-m", oldName, newName}, &ok);
    Q_EMIT operationCompleted("renameBranch", ok,
        ok ? QString("Branch renamed to '%1'").arg(newName) : "Branch rename failed");
    return ok;
}

// === Stash Operations ===

bool GitManager::stash(const QString& message) {
    QStringList args = {"stash", "push"};
    if (!message.isEmpty()) {
        args << "-m" << message;
    }
    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("stash", ok, ok ? "Changes stashed" : "Stash failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::stashPop() {
    bool ok = false;
    d->runGit({"stash", "pop"}, &ok);
    Q_EMIT operationCompleted("stashPop", ok, ok ? "Stash popped" : "Stash pop failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::stashApply(int index) {
    QString ref = QString("stash@{%1}").arg(index);
    bool ok = false;
    d->runGit({"stash", "apply", ref}, &ok);
    Q_EMIT operationCompleted("stashApply", ok, ok ? "Stash applied" : "Stash apply failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::stashDrop(int index) {
    QString ref = QString("stash@{%1}").arg(index);
    bool ok = false;
    d->runGit({"stash", "drop", ref}, &ok);
    Q_EMIT operationCompleted("stashDrop", ok, ok ? "Stash dropped" : "Stash drop failed");
    return ok;
}

bool GitManager::stashClear() {
    bool ok = false;
    d->runGit({"stash", "clear"}, &ok);
    Q_EMIT operationCompleted("stashClear", ok, ok ? "All stashes cleared" : "Stash clear failed");
    return ok;
}

QStringList GitManager::stashList() const {
    bool ok = false;
    QStringList lines = d->runGit({"stash", "list", "--format=%gd: %s"}, &ok);
    return lines;
}

// === Tag Operations ===

bool GitManager::tag(const QString& name, const QString& message) {
    QStringList args = {"tag"};
    if (!message.isEmpty()) {
        args << "-a" << "-m" << message;
    }
    args << name;
    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("tag", ok,
        ok ? QString("Tag '%1' created").arg(name) : "Tag creation failed");
    return ok;
}

bool GitManager::tagDelete(const QString& name) {
    bool ok = false;
    d->runGit({"tag", "-d", name}, &ok);
    Q_EMIT operationCompleted("tagDelete", ok,
        ok ? QString("Tag '%1' deleted").arg(name) : "Tag deletion failed");
    return ok;
}

bool GitManager::pushTag(const QString& name, const QString& remote) {
    QString r = remote.isEmpty() ? "origin" : remote;
    bool ok = false;
    d->runGit({"push", r, name}, &ok);
    Q_EMIT operationCompleted("pushTag", ok,
        ok ? QString("Tag '%1' pushed to %2").arg(name, r) : "Tag push failed");
    return ok;
}

QStringList GitManager::tags() const {
    bool ok = false;
    QStringList lines = d->runGit({"tag", "-l"}, &ok);
    return lines;
}

// === Status and Log ===

QList<StatusEntry> GitManager::status() const {
    QList<StatusEntry> result;
    if (d->repoPath.isEmpty()) return result;

    bool ok = false;
    QStringList lines = d->runGit(
        {"status", "--porcelain=v1", "-uall"}, &ok);
    if (!ok) return result;

    for (const QString& line : lines) {
        if (line.size() < 3) continue;

        StatusEntry entry;
        entry.indexStatus = line[0].toLatin1();
        entry.worktreeStatus = line[1].toLatin1();

        // Parse path (handles renames)
        QString paths = line.mid(3);
        if (paths.contains(" -> ")) {
            QStringList parts = paths.split(" -> ");
            entry.originalPath = parts[0];
            entry.path = parts[1];
            entry.isRenamed = true;
        } else {
            entry.path = paths;
        }

        entry.isStaged = (entry.indexStatus != ' ' && entry.indexStatus != '?');
        entry.isUntracked = (entry.indexStatus == '?' && entry.worktreeStatus == '?');
        entry.isIgnored = (entry.indexStatus == '!' && entry.worktreeStatus == '!');
        entry.isUnmerged = (entry.indexStatus == 'U' || entry.worktreeStatus == 'U' ||
                            entry.indexStatus == 'D' && entry.worktreeStatus == 'D' ||
                            entry.indexStatus == 'A' && entry.worktreeStatus == 'A');

        result.append(entry);
    }

    return result;
}

QList<CommitInfo> GitManager::log(int count, const QString& branch) const {
    QList<CommitInfo> result;
    if (d->repoPath.isEmpty()) return result;

    QStringList args = {"log", QString("-%1").arg(count), "--format=%H|%h|%an|%ae|%at|%s|%p"};
    if (!branch.isEmpty()) args << branch;

    bool ok = false;
    QStringList lines = d->runGit(args, &ok);
    if (!ok) return result;

    for (const QString& line : lines) {
        QStringList parts = line.split('|');
        if (parts.size() < 6) continue;

        CommitInfo ci;
        ci.hash = parts[0];
        ci.shortHash = parts[1];
        ci.author = parts[2];
        ci.email = parts[3];
        ci.date = QDateTime::fromSecsSinceEpoch(parts[4].toLongLong());
        ci.subject = parts[5];
        ci.message = parts[5];
        if (parts.size() > 6) {
            ci.parents = parts[6].split(' ', Qt::SkipEmptyParts);
        }

        result.append(ci);
    }

    return result;
}

QList<FileDiff> GitManager::diff(const QString& from, const QString& to) const {
    QList<FileDiff> result;
    if (d->repoPath.isEmpty()) return result;

    bool ok = false;
    QStringList lines = d->runGit({"diff", "-U3", from, to}, &ok);
    if (!ok || lines.isEmpty()) return result;

    return parseDiff(lines);
}

QList<FileDiff> GitManager::diffUnstaged() const {
    QList<FileDiff> result;
    if (d->repoPath.isEmpty()) return result;

    bool ok = false;
    QStringList lines = d->runGit({"diff"}, &ok);
    if (!ok) return result;

    return parseDiff(lines);
}

QList<FileDiff> GitManager::diffStaged() const {
    QList<FileDiff> result;
    if (d->repoPath.isEmpty()) return result;

    bool ok = false;
    QStringList lines = d->runGit({"diff", "--cached"}, &ok);
    if (!ok) return result;

    return parseDiff(lines);
}

QList<FileDiff> GitManager::diffFile(const QString& file) const {
    QList<FileDiff> result;
    if (d->repoPath.isEmpty()) return result;

    bool ok = false;
    QStringList lines = d->runGit({"diff", "--", file}, &ok);
    if (!ok) return result;

    return parseDiff(lines);
}

QList<FileDiff> GitManager::parseDiff(const QStringList& lines) const {
    QList<FileDiff> result;
    FileDiff current;
    DiffHunk currentHunk;
    bool inHunk = false;

    for (const QString& line : lines) {
        if (line.startsWith("diff --git ")) {
            if (!current.oldPath.isEmpty() || !current.newPath.isEmpty()) {
                if (inHunk) {
                    current.hunks.append(currentHunk);
                    inHunk = false;
                }
                result.append(current);
            }
            current = FileDiff();
            currentHunk = DiffHunk();
            inHunk = false;
        } else if (line.startsWith("--- ")) {
            current.oldPath = line.mid(4);
            if (current.oldPath == "/dev/null") current.isNew = true;
        } else if (line.startsWith("+++ ")) {
            current.newPath = line.mid(4);
            if (current.newPath == "/dev/null") current.isDeleted = true;
        } else if (line.startsWith("old mode ")) {
            current.oldMode = line.mid(9);
        } else if (line.startsWith("new mode ")) {
            current.newMode = line.mid(9);
        } else if (line.startsWith("rename from ")) {
            current.oldPath = line.mid(12);
            current.isRenamed = true;
        } else if (line.startsWith("rename to ")) {
            current.newPath = line.mid(10);
            current.isRenamed = true;
        } else if (line.startsWith("@@ ")) {
            if (inHunk) {
                current.hunks.append(currentHunk);
            }
            currentHunk = DiffHunk();
            currentHunk.header = line;
            inHunk = true;

            // Parse hunk header: @@ -oldStart,oldLines +newStart,newLines @@
            static const QRegularExpression hunkRe(
                "@@\\s*-(\\d+)(?:,(\\d+))?\\s*\\+(\\d+)(?:,(\\d+))?\\s*@@");
            QRegularExpressionMatch match = hunkRe.match(line);
            if (match.hasMatch()) {
                currentHunk.oldStart = match.captured(1).toInt();
                currentHunk.oldLines = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
                currentHunk.newStart = match.captured(3).toInt();
                currentHunk.newLines = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();
            }
        } else if (inHunk) {
            currentHunk.lines.append(line);
        }
    }

    if (!current.oldPath.isEmpty() || !current.newPath.isEmpty()) {
        if (inHunk) {
            current.hunks.append(currentHunk);
        }
        result.append(current);
    }

    return result;
}

QString GitManager::show(const QString& hash, const QString& file) const {
    if (d->repoPath.isEmpty()) return QString();

    QStringList args = {"show", hash};
    if (!file.isEmpty()) {
        args << "--" << file;
    }

    bool ok = false;
    return d->runGitStr(args, &ok);
}

// === Information ===

CommitInfo GitManager::getCommit(const QString& hash) const {
    CommitInfo ci;
    if (d->repoPath.isEmpty()) return ci;

    bool ok = false;
    QStringList lines = d->runGit(
        {"show", "-s", "--format=%H|%h|%an|%ae|%at|%B", hash}, &ok);
    if (!ok || lines.isEmpty()) return ci;

    QString firstLine = lines[0];
    QStringList parts = firstLine.split('|');
    if (parts.size() >= 6) {
        ci.hash = parts[0];
        ci.shortHash = parts[1];
        ci.author = parts[2];
        ci.email = parts[3];
        ci.date = QDateTime::fromSecsSinceEpoch(parts[4].toLongLong());
        ci.subject = parts[5];

        // Reconstruct full message
        ci.message = parts[5];
        for (int i = 1; i < lines.size(); ++i) {
            ci.message += "\n" + lines[i];
        }
    }

    return ci;
}

QString GitManager::blame(const QString& file, int line) const {
    if (d->repoPath.isEmpty()) return QString();

    QStringList args = {"blame", "--porcelain"};
    if (line > 0) {
        args << QString("-L %1,%1").arg(line);
    }
    args << "--" << file;

    bool ok = false;
    return d->runGitStr(args, &ok);
}

QString GitManager::currentBranch() const {
    if (d->repoPath.isEmpty()) return QString();

    bool ok = false;
    QString result = d->runGitStr({"rev-parse", "--abbrev-ref", "HEAD"}, &ok);
    return ok ? result.trimmed() : QString();
}

MergeState GitManager::mergeState() const {
    if (d->repoPath.isEmpty()) return MergeState::None;

    QDir repoDir(d->repoPath);
    QDir gitDir(repoDir.filePath(".git"));

    if (gitDir.exists("MERGE_HEAD")) return MergeState::Merging;
    if (gitDir.exists("CHERRY_PICK_HEAD")) return MergeState::CherryPicking;
    if (gitDir.exists("REVERT_HEAD")) return MergeState::Reverting;
    if (gitDir.exists("rebase-apply") || gitDir.exists("rebase-merge")) return MergeState::Rebasing;
    if (gitDir.exists("BISECT_LOG")) return MergeState::BISECTing;

    return MergeState::None;
}

// === Configuration ===

bool GitManager::setConfig(const QString& key, const QString& value, bool global) {
    QStringList args = {"config"};
    if (global) args << "--global";
    args << key << value;
    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("setConfig", ok, ok ? "Config set" : "Config failed");
    return ok;
}

QString GitManager::getConfig(const QString& key, bool global) const {
    QStringList args = {"config"};
    if (global) args << "--global";
    args << "--get" << key;
    bool ok = false;
    return d->runGitStr(args, &ok).trimmed();
}

// === Reset Operations ===

bool GitManager::resetSoft(const QString& hash) {
    bool ok = false;
    d->runGit({"reset", "--soft", hash}, &ok);
    Q_EMIT operationCompleted("resetSoft", ok, ok ? "Soft reset done" : "Reset failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::resetMixed(const QString& hash) {
    bool ok = false;
    d->runGit({"reset", "--mixed", hash}, &ok);
    Q_EMIT operationCompleted("resetMixed", ok, ok ? "Mixed reset done" : "Reset failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::resetHard(const QString& hash) {
    bool ok = false;
    d->runGit({"reset", "--hard", hash}, &ok);
    Q_EMIT operationCompleted("resetHard", ok, ok ? "Hard reset done" : "Reset failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::clean(const QString& path) {
    QStringList args = {"clean", "-fd"};
    if (!path.isEmpty()) args << "--" << path;
    bool ok = false;
    d->runGit(args, &ok);
    Q_EMIT operationCompleted("clean", ok, ok ? "Clean done" : "Clean failed");
    if (ok) Q_EMIT statusChanged();
    return ok;
}

bool GitManager::cleanAll() {
    return clean(QString());
}

// === Submodule Operations ===

bool GitManager::submoduleAdd(const QString& url, const QString& path) {
    bool ok = false;
    d->runGit({"submodule", "add", url, path}, &ok, 120000);
    Q_EMIT operationCompleted("submoduleAdd", ok,
        ok ? "Submodule added" : "Submodule add failed");
    return ok;
}

bool GitManager::submoduleUpdate(bool init) {
    QStringList args = {"submodule", "update"};
    if (init) args << "--init";
    args << "--recursive";
    bool ok = false;
    d->runGit(args, &ok, 120000);
    Q_EMIT operationCompleted("submoduleUpdate", ok,
        ok ? "Submodules updated" : "Submodule update failed");
    return ok;
}

bool GitManager::submoduleSync() {
    bool ok = false;
    d->runGit({"submodule", "sync", "--recursive"}, &ok);
    Q_EMIT operationCompleted("submoduleSync", ok,
        ok ? "Submodules synced" : "Submodule sync failed");
    return ok;
}

} // namespace powsys365::ide::git
