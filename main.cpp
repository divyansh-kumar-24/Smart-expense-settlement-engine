
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <queue>
#include <set>
#include <sstream>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using namespace std;

static string nowUtcIso8601() {
    auto now = chrono::system_clock::now();
    time_t tt = chrono::system_clock::to_time_t(now);
    tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

static string trim(const string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static string jsonEscape(const string& s) {
    string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    stringstream ss;
                    ss << "\\u" << hex << setw(4) << setfill('0') << (int)(unsigned char)c;
                    out += ss.str();
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static string toJsonString(const string& s) { return "\"" + jsonEscape(s) + "\""; }
static string toJsonNumber(long long v) { return to_string(v); }
static string toJsonDouble(double v) {
    stringstream ss;
    ss << fixed << setprecision(2) << v;
    string out = ss.str();
    while (!out.empty() && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.push_back('0');
    return out;
}

static bool startsWith(const string& s, const string& pref) {
    return s.rfind(pref, 0) == 0;
}

static vector<string> split(const string& s, char delim) {
    vector<string> parts;
    string cur;
    stringstream ss(s);
    while (getline(ss, cur, delim)) parts.push_back(cur);
    return parts;
}

static string lowerStr(string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Extremely small JSON helper for controlled payloads.
static string extractStringField(const string& body, const string& key, const string& def = "") {
    string pat = "\"" + key + "\"";
    size_t p = body.find(pat);
    if (p == string::npos) return def;
    p = body.find(':', p + pat.size());
    if (p == string::npos) return def;
    p = body.find('"', p + 1);
    if (p == string::npos) return def;
    size_t q = body.find('"', p + 1);
    if (q == string::npos) return def;
    return body.substr(p + 1, q - p - 1);
}

static bool extractBoolField(const string& body, const string& key, bool def = false) {
    string pat = "\"" + key + "\"";
    size_t p = body.find(pat);
    if (p == string::npos) return def;
    p = body.find(':', p + pat.size());
    if (p == string::npos) return def;
    string tail = lowerStr(trim(body.substr(p + 1, 16)));
    if (tail.rfind("true", 0) == 0) return true;
    if (tail.rfind("false", 0) == 0) return false;
    return def;
}

static long long extractLLField(const string& body, const string& key, long long def = 0) {
    string pat = "\"" + key + "\"";
    size_t p = body.find(pat);
    if (p == string::npos) return def;
    p = body.find(':', p + pat.size());
    if (p == string::npos) return def;
    size_t i = body.find_first_of("-0123456789", p + 1);
    if (i == string::npos) return def;
    size_t j = i;
    while (j < body.size() && (isdigit((unsigned char)body[j]) || body[j] == '-' || body[j] == '+')) j++;
    try { return stoll(body.substr(i, j - i)); } catch (...) { return def; }
}

static double extractDoubleField(const string& body, const string& key, double def = 0.0) {
    string pat = "\"" + key + "\"";
    size_t p = body.find(pat);
    if (p == string::npos) return def;
    p = body.find(':', p + pat.size());
    if (p == string::npos) return def;
    size_t i = body.find_first_of("-0123456789", p + 1);
    if (i == string::npos) return def;
    size_t j = i;
    while (j < body.size() && (isdigit((unsigned char)body[j]) || body[j] == '.' || body[j] == '-' || body[j] == '+' || body[j] == 'e' || body[j] == 'E')) j++;
    try { return stod(body.substr(i, j - i)); } catch (...) { return def; }
}

struct SplitInput {
    long long user_id{};
    double amount{};      // exact/equal final amount in rupees
    double percentage{};  // percentage split input
};

static vector<SplitInput> extractSplits(const string& body) {
    vector<SplitInput> out;
    string key = "\"splits\"";
    size_t p = body.find(key);
    if (p == string::npos) return out;
    p = body.find('[', p);
    if (p == string::npos) return out;
    int depth = 0;
    size_t start = string::npos;
    for (size_t i = p; i < body.size(); ++i) {
        if (body[i] == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (body[i] == '}') {
            depth--;
            if (depth == 0 && start != string::npos) {
                string obj = body.substr(start, i - start + 1);
                SplitInput s;
                s.user_id = extractLLField(obj, "user_id", 0);
                s.amount = extractDoubleField(obj, "amount", 0.0);
                s.percentage = extractDoubleField(obj, "percentage", 0.0);
                out.push_back(s);
                start = string::npos;
            }
        } else if (body[i] == ']') {
            break;
        }
    }
    return out;
}

enum class SplitType { EQUAL, EXACT, PERCENTAGE };

static string splitTypeToString(SplitType t) {
    switch (t) {
        case SplitType::EQUAL: return "EQUAL";
        case SplitType::EXACT: return "EXACT";
        case SplitType::PERCENTAGE: return "PERCENTAGE";
    }
    return "EQUAL";
}

static SplitType parseSplitType(const string& s) {
    string x = lowerStr(trim(s));
    if (x == "exact") return SplitType::EXACT;
    if (x == "percentage") return SplitType::PERCENTAGE;
    return SplitType::EQUAL;
}

struct User {
    long long id{};
    string name;
    string email;
    string created_at;
};

struct Group {
    long long id{};
    string name;
    long long owner_id{};
    string created_at;
};

struct Expense {
    long long id{};
    long long group_id{};
    long long paid_by{};
    double amount{};
    string note;
    SplitType split_type{SplitType::EQUAL};
    string created_at;
};

struct Settlement {
    long long from_user{};
    long long to_user{};
    double amount{};
};

class Database {
    sqlite3* db_{nullptr};

public:
    explicit Database(const string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            string err = sqlite3_errmsg(db_);
            throw runtime_error("Failed to open database: " + err);
        }
        exec("PRAGMA foreign_keys = ON;");
    }

    ~Database() {
        if (db_) sqlite3_close(db_);
    }

    sqlite3* raw() { return db_; }

    void exec(const string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            string e = err ? err : "unknown error";
            sqlite3_free(err);
            throw runtime_error("SQL exec failed: " + e + " | SQL=" + sql);
        }
    }

    void initSchema() {
        exec(R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            email TEXT NOT NULL UNIQUE,
            created_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS groups_table (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            owner_id INTEGER NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(owner_id) REFERENCES users(id)
        );

        CREATE TABLE IF NOT EXISTS group_members (
            group_id INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            created_at TEXT NOT NULL,
            PRIMARY KEY(group_id, user_id),
            FOREIGN KEY(group_id) REFERENCES groups_table(id) ON DELETE CASCADE,
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS expenses (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            group_id INTEGER NOT NULL,
            paid_by INTEGER NOT NULL,
            amount REAL NOT NULL CHECK(amount >= 0),
            note TEXT,
            split_type TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(group_id) REFERENCES groups_table(id) ON DELETE CASCADE,
            FOREIGN KEY(paid_by) REFERENCES users(id)
        );

        CREATE TABLE IF NOT EXISTS expense_splits (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            expense_id INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            split_amount REAL NOT NULL CHECK(split_amount >= 0),
            FOREIGN KEY(expense_id) REFERENCES expenses(id) ON DELETE CASCADE,
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_group_members_group ON group_members(group_id);
        CREATE INDEX IF NOT EXISTS idx_expenses_group ON expenses(group_id);
        CREATE INDEX IF NOT EXISTS idx_expense_splits_expense ON expense_splits(expense_id);
        )SQL");
    }
};

class UserRepository {
    Database& db_;
public:
    explicit UserRepository(Database& db) : db_(db) {}

    long long create(const string& name, const string& email) {
        const char* sql = "INSERT INTO users(name,email,created_at) VALUES(?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
        string now = nowUtcIso8601();
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            string e = sqlite3_errmsg(db_.raw());
            sqlite3_finalize(stmt);
            throw runtime_error("Create user failed: " + e);
        }
        sqlite3_finalize(stmt);
        return (long long)sqlite3_last_insert_rowid(db_.raw());
    }

    vector<User> getAll() {
        vector<User> out;
        const char* sql = "SELECT id,name,email,created_at FROM users ORDER BY id;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            User u;
            u.id = sqlite3_column_int64(stmt, 0);
            u.name = (const char*)sqlite3_column_text(stmt, 1);
            u.email = (const char*)sqlite3_column_text(stmt, 2);
            u.created_at = (const char*)sqlite3_column_text(stmt, 3);
            out.push_back(u);
        }
        sqlite3_finalize(stmt);
        return out;
    }

    bool exists(long long id) {
        const char* sql = "SELECT 1 FROM users WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_int64(stmt, 1, id);
        bool ok = sqlite3_step(stmt) == SQLITE_ROW;
        sqlite3_finalize(stmt);
        return ok;
    }
};

class GroupRepository {
    Database& db_;
public:
    explicit GroupRepository(Database& db) : db_(db) {}

    long long create(const string& name, long long ownerId) {
        const char* sql = "INSERT INTO groups_table(name,owner_id,created_at) VALUES(?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, ownerId);
        string now = nowUtcIso8601();
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            string e = sqlite3_errmsg(db_.raw());
            sqlite3_finalize(stmt);
            throw runtime_error("Create group failed: " + e);
        }
        sqlite3_finalize(stmt);
        return (long long)sqlite3_last_insert_rowid(db_.raw());
    }

    bool exists(long long groupId) {
        const char* sql = "SELECT 1 FROM groups_table WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_int64(stmt, 1, groupId);
        bool ok = sqlite3_step(stmt) == SQLITE_ROW;
        sqlite3_finalize(stmt);
        return ok;
    }

    void addMember(long long groupId, long long userId) {
        const char* sql = "INSERT OR IGNORE INTO group_members(group_id,user_id,created_at) VALUES(?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_int64(stmt, 1, groupId);
        sqlite3_bind_int64(stmt, 2, userId);
        string now = nowUtcIso8601();
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            string e = sqlite3_errmsg(db_.raw());
            sqlite3_finalize(stmt);
            throw runtime_error("Add member failed: " + e);
        }
        sqlite3_finalize(stmt);
    }

    vector<long long> getMembers(long long groupId) {
        vector<long long> members;
        const char* sql = "SELECT user_id FROM group_members WHERE group_id=? ORDER BY user_id;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_int64(stmt, 1, groupId);
        while (sqlite3_step(stmt) == SQLITE_ROW) members.push_back(sqlite3_column_int64(stmt, 0));
        sqlite3_finalize(stmt);
        return members;
    }
};

class ExpenseRepository {
    Database& db_;
public:
    explicit ExpenseRepository(Database& db) : db_(db) {}

    long long create(long long groupId, long long paidBy, double amount, const string& note, SplitType type) {
        const char* sql = "INSERT INTO expenses(group_id,paid_by,amount,note,split_type,created_at) VALUES(?,?,?,?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_int64(stmt, 1, groupId);
        sqlite3_bind_int64(stmt, 2, paidBy);
        sqlite3_bind_double(stmt, 3, amount);
        sqlite3_bind_text(stmt, 4, note.c_str(), -1, SQLITE_TRANSIENT);
        string st = splitTypeToString(type);
        sqlite3_bind_text(stmt, 5, st.c_str(), -1, SQLITE_TRANSIENT);
        string now = nowUtcIso8601();
        sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            string e = sqlite3_errmsg(db_.raw());
            sqlite3_finalize(stmt);
            throw runtime_error("Create expense failed: " + e);
        }
        sqlite3_finalize(stmt);
        return (long long)sqlite3_last_insert_rowid(db_.raw());
    }

    void addSplit(long long expenseId, long long userId, double splitAmount) {
        const char* sql = "INSERT INTO expense_splits(expense_id,user_id,split_amount) VALUES(?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_int64(stmt, 1, expenseId);
        sqlite3_bind_int64(stmt, 2, userId);
        sqlite3_bind_double(stmt, 3, splitAmount);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            string e = sqlite3_errmsg(db_.raw());
            sqlite3_finalize(stmt);
            throw runtime_error("Add split failed: " + e);
        }
        sqlite3_finalize(stmt);
    }
};

class SplitwiseService {
    Database& db_;
    UserRepository users_;
    GroupRepository groups_;
    ExpenseRepository expenses_;

    static bool approxEqual(double a, double b, double eps = 1e-2) {
        return fabs(a - b) <= eps;
    }

public:
    explicit SplitwiseService(Database& db) : db_(db), users_(db), groups_(db), expenses_(db) {}

    long long createUser(const string& name, const string& email) {
        if (name.empty() || email.empty()) throw runtime_error("name/email cannot be empty");
        return users_.create(name, email);
    }

    long long createGroup(const string& name, long long ownerId) {
        if (!users_.exists(ownerId)) throw runtime_error("owner user does not exist");
        long long gid = groups_.create(name, ownerId);
        groups_.addMember(gid, ownerId); // owner is automatically a member
        return gid;
    }

    void addMember(long long groupId, long long userId) {
        if (!groups_.exists(groupId)) throw runtime_error("group does not exist");
        if (!users_.exists(userId)) throw runtime_error("user does not exist");
        groups_.addMember(groupId, userId);
    }

    struct GroupLedger {
        map<long long, double> net;  // positive => receives, negative => owes
        vector<pair<long long, double>> userOwes;
        vector<pair<long long, double>> userGets;
    };

    GroupLedger buildLedger(long long groupId) {
        GroupLedger ledger;
        const char* sql = R"SQL(
            SELECT e.paid_by, es.user_id, es.split_amount, e.amount
            FROM expenses e
            JOIN expense_splits es ON e.id = es.expense_id
            WHERE e.group_id = ?
        )SQL";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) throw runtime_error(sqlite3_errmsg(db_.raw()));
        sqlite3_bind_int64(stmt, 1, groupId);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            long long paidBy = sqlite3_column_int64(stmt, 0);
            long long splitUser = sqlite3_column_int64(stmt, 1);
            double splitAmount = sqlite3_column_double(stmt, 2);

            ledger.net[paidBy] += splitAmount;
            ledger.net[splitUser] -= splitAmount;
        }
        sqlite3_finalize(stmt);

        for (auto& [uid, bal] : ledger.net) {
            if (bal < -1e-9) ledger.userOwes.push_back({uid, bal});
            else if (bal > 1e-9) ledger.userGets.push_back({uid, bal});
        }
        return ledger;
    }

    vector<Settlement> simplifyDebt(const map<long long, double>& net) {
        struct Node {
            long long user;
            double amt;
        };
        auto cmpCredit = [](const Node& a, const Node& b) { return a.amt < b.amt; };
        auto cmpDebt = [](const Node& a, const Node& b) { return a.amt < b.amt; };

        priority_queue<Node, vector<Node>, decltype(cmpCredit)> creditors(cmpCredit);
        priority_queue<Node, vector<Node>, decltype(cmpDebt)> debtors(cmpDebt);

        for (auto& [uid, bal] : net) {
            if (bal > 1e-9) creditors.push({uid, bal});
            else if (bal < -1e-9) debtors.push({uid, -bal});
        }

        vector<Settlement> ans;
        while (!creditors.empty() && !debtors.empty()) {
            auto c = creditors.top(); creditors.pop();
            auto d = debtors.top(); debtors.pop();
            double x = min(c.amt, d.amt);

            ans.push_back({d.user, c.user, x});
            c.amt -= x;
            d.amt -= x;
            if (c.amt > 1e-9) creditors.push(c);
            if (d.amt > 1e-9) debtors.push(d);
        }
        return ans;
    }

    long long addExpense(long long groupId, long long paidBy, double amount, const string& note, SplitType type, const vector<SplitInput>& inputs) {
        if (!groups_.exists(groupId)) throw runtime_error("group does not exist");
        if (!users_.exists(paidBy)) throw runtime_error("payer does not exist");
        auto members = groups_.getMembers(groupId);
        if (members.empty()) throw runtime_error("group has no members");

        set<long long> memberSet(members.begin(), members.end());

        vector<pair<long long, double>> splits;
        if (type == SplitType::EQUAL) {
            if (!inputs.empty()) throw runtime_error("equal split should not include custom splits");
            double each = round((amount / members.size()) * 100.0) / 100.0;
            double sum = 0.0;
            for (size_t i = 0; i < members.size(); ++i) {
                double part = (i + 1 == members.size()) ? round((amount - sum) * 100.0) / 100.0 : each;
                sum += part;
                splits.push_back({members[i], part});
            }
        } else if (type == SplitType::EXACT) {
            if (inputs.size() != members.size()) throw runtime_error("exact split requires one split per member");
            double sum = 0.0;
            for (auto& s : inputs) {
                if (!memberSet.count(s.user_id)) throw runtime_error("split user is not a group member");
                if (s.amount < 0) throw runtime_error("split amount cannot be negative");
                splits.push_back({s.user_id, s.amount});
                sum += s.amount;
            }
            if (!approxEqual(sum, amount)) throw runtime_error("exact splits do not sum to total amount");
        } else {
            if (inputs.size() != members.size()) throw runtime_error("percentage split requires one entry per member");
            double sumPct = 0.0;
            vector<double> raw;
            raw.reserve(inputs.size());
            for (auto& s : inputs) {
                if (!memberSet.count(s.user_id)) throw runtime_error("split user is not a group member");
                if (s.percentage < 0) throw runtime_error("percentage cannot be negative");
                sumPct += s.percentage;
            }
            if (!approxEqual(sumPct, 100.0)) throw runtime_error("percentages must sum to 100");
            double total = 0.0;
            for (size_t i = 0; i < inputs.size(); ++i) {
                double part = round((amount * inputs[i].percentage / 100.0) * 100.0) / 100.0;
                raw.push_back(part);
                total += part;
            }
            double diff = round((amount - total) * 100.0) / 100.0;
            if (!raw.empty()) raw.back() += diff;
            for (size_t i = 0; i < inputs.size(); ++i) splits.push_back({inputs[i].user_id, raw[i]});
        }

        db_.exec("BEGIN TRANSACTION;");
        try {
            long long expId = expenses_.create(groupId, paidBy, amount, note, type);
            for (auto& [uid, part] : splits) expenses_.addSplit(expId, uid, part);
            db_.exec("COMMIT;");
            return expId;
        } catch (...) {
            try { db_.exec("ROLLBACK;"); } catch (...) {}
            throw;
        }
    }

    string usersJson() {
        auto us = users_.getAll();
        string out = "[";
        for (size_t i = 0; i < us.size(); ++i) {
            if (i) out += ",";
            out += "{";
            out += "\"id\":" + toJsonNumber(us[i].id) + ",";
            out += "\"name\":" + toJsonString(us[i].name) + ",";
            out += "\"email\":" + toJsonString(us[i].email) + ",";
            out += "\"created_at\":" + toJsonString(us[i].created_at);
            out += "}";
        }
        out += "]";
        return out;
    }

    string ledgerJson(long long groupId) {
        if (!groups_.exists(groupId)) throw runtime_error("group does not exist");
        auto ledger = buildLedger(groupId);
        string out = "{";
        out += "\"group_id\":" + toJsonNumber(groupId) + ",";
        out += "\"balances\":[";
        bool first = true;
        for (auto& [uid, bal] : ledger.net) {
            if (!first) out += ",";
            first = false;
            out += "{";
            out += "\"user_id\":" + toJsonNumber(uid) + ",";
            out += "\"net\":" + toJsonDouble(bal);
            out += "}";
        }
        out += "]}";
        return out;
    }

    string settlementsJson(long long groupId) {
        if (!groups_.exists(groupId)) throw runtime_error("group does not exist");
        auto ledger = buildLedger(groupId);
        auto settlements = simplifyDebt(ledger.net);
        string out = "{";
        out += "\"group_id\":" + toJsonNumber(groupId) + ",";
        out += "\"settlements\":[";
        for (size_t i = 0; i < settlements.size(); ++i) {
            if (i) out += ",";
            out += "{";
            out += "\"from_user\":" + toJsonNumber(settlements[i].from_user) + ",";
            out += "\"to_user\":" + toJsonNumber(settlements[i].to_user) + ",";
            out += "\"amount\":" + toJsonDouble(settlements[i].amount);
            out += "}";
        }
        out += "]}";
        return out;
    }
};

struct HttpRequest {
    string method;
    string path;
    string body;
    map<string, string> headers;
};

static string httpResponse(int code, const string& body, const string& contentType = "application/json") {
    string reason = "OK";
    if (code == 201) reason = "Created";
    else if (code == 400) reason = "Bad Request";
    else if (code == 404) reason = "Not Found";
    else if (code == 500) reason = "Internal Server Error";
    stringstream ss;
    ss << "HTTP/1.1 " << code << " " << reason << "\r\n";
    ss << "Content-Type: " << contentType << "\r\n";
    ss << "Content-Length: " << body.size() << "\r\n";
    ss << "Connection: close\r\n\r\n";
    ss << body;
    return ss.str();
}

static HttpRequest parseHttpRequest(const string& raw) {
    HttpRequest req;
    size_t pos = raw.find("\r\n");
    string first = raw.substr(0, pos);
    {
        stringstream ss(first);
        ss >> req.method >> req.path;
    }
    size_t headerEnd = raw.find("\r\n\r\n");
    string headerPart = raw.substr(pos + 2, headerEnd == string::npos ? string::npos : headerEnd - (pos + 2));
    stringstream hs(headerPart);
    string line;
    getline(hs, line); // skip first line
    while (getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t c = line.find(':');
        if (c != string::npos) {
            string k = trim(line.substr(0, c));
            string v = trim(line.substr(c + 1));
            req.headers[k] = v;
        }
    }
    if (headerEnd != string::npos) req.body = raw.substr(headerEnd + 4);
    return req;
}

class HttpServer {
    int port_;
    SplitwiseService& service_;

    static bool matchPrefix(const string& path, const string& pref) {
        return path.rfind(pref, 0) == 0;
    }

    static vector<string> pathParts(const string& p) {
        vector<string> parts;
        for (auto& x : split(p, '/')) if (!x.empty()) parts.push_back(x);
        return parts;
    }

public:
    HttpServer(int port, SplitwiseService& service) : port_(port), service_(service) {}

    string handle(const HttpRequest& req) {
        try {
            auto parts = pathParts(req.path);

            if (req.method == "GET" && req.path == "/health") {
                return httpResponse(200, "{\"status\":\"ok\"}");
            }

            if (req.method == "POST" && req.path == "/users") {
                string name = extractStringField(req.body, "name");
                string email = extractStringField(req.body, "email");
                long long id = service_.createUser(name, email);
                return httpResponse(201, string("{\"id\":") + toJsonNumber(id) + "}");
            }

            if (req.method == "GET" && req.path == "/users") {
                return httpResponse(200, service_.usersJson());
            }

            if (req.method == "POST" && req.path == "/groups") {
                string name = extractStringField(req.body, "name");
                long long owner_id = extractLLField(req.body, "owner_id");
                long long id = service_.createGroup(name, owner_id);
                return httpResponse(201, string("{\"id\":") + toJsonNumber(id) + "}");
            }

            if (req.method == "POST" && req.path == "/groups/members") {
                long long group_id = extractLLField(req.body, "group_id");
                long long user_id = extractLLField(req.body, "user_id");
                service_.addMember(group_id, user_id);
                return httpResponse(200, "{\"status\":\"member_added\"}");
            }

            if (req.method == "POST" && req.path == "/expenses") {
                long long group_id = extractLLField(req.body, "group_id");
                long long paid_by = extractLLField(req.body, "paid_by");
                double amount = extractDoubleField(req.body, "amount");
                string note = extractStringField(req.body, "note");
                SplitType type = parseSplitType(extractStringField(req.body, "split_type", "equal"));
                auto splits = extractSplits(req.body);
                long long id = service_.addExpense(group_id, paid_by, amount, note, type, splits);
                return httpResponse(201, string("{\"id\":") + toJsonNumber(id) + "}");
            }

            if (req.method == "GET" && parts.size() == 3 && parts[0] == "groups" && parts[2] == "balances") {
                long long gid = stoll(parts[1]);
                return httpResponse(200, service_.ledgerJson(gid));
            }

            if (req.method == "GET" && parts.size() == 3 && parts[0] == "groups" && parts[2] == "settlements") {
                long long gid = stoll(parts[1]);
                return httpResponse(200, service_.settlementsJson(gid));
            }

            if (req.method == "GET" && req.path == "/") {
                return httpResponse(200, "{\"message\":\"Splitwise clone backend is running\"}");
            }

            return httpResponse(404, "{\"error\":\"route not found\"}");
        } catch (const exception& e) {
            string msg = e.what();
            string body = string("{\"error\":") + toJsonString(msg) + "}";
            if (msg.find("does not exist") != string::npos || msg.find("route") != string::npos) return httpResponse(404, body);
            return httpResponse(400, body);
        }
    }

    void run() {
        int serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0) throw runtime_error("socket creation failed");

        int opt = 1;
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons((uint16_t)port_);

        if (bind(serverFd, (sockaddr*)&addr, sizeof(addr)) < 0) throw runtime_error("bind failed: " + string(strerror(errno)));
        if (listen(serverFd, 16) < 0) throw runtime_error("listen failed");

        cerr << "Splitwise backend running on port " << port_ << "\n";

        while (true) {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            int fd = accept(serverFd, (sockaddr*)&client, &len);
            if (fd < 0) continue;

            thread([this, fd]() {
                try {
                    string raw;
                    char buf[4096];
                    ssize_t n;
                    size_t contentLen = 0;
                    bool headerDone = false;
                    size_t headerEnd = string::npos;

                    while ((n = read(fd, buf, sizeof(buf))) > 0) {
                        raw.append(buf, buf + n);
                        if (!headerDone) {
                            headerEnd = raw.find("\r\n\r\n");
                            if (headerEnd != string::npos) {
                                headerDone = true;
                                string headerBlock = raw.substr(0, headerEnd);
                                auto lines = split(headerBlock, '\n');
                                for (auto& ln : lines) {
                                    string line = ln;
                                    if (!line.empty() && line.back() == '\r') line.pop_back();
                                    size_t c = line.find(':');
                                    if (c != string::npos) {
                                        string k = trim(line.substr(0, c));
                                        string v = trim(line.substr(c + 1));
                                        if (lowerStr(k) == "content-length") contentLen = (size_t)stoll(v);
                                    }
                                }
                                size_t bodyHave = raw.size() - (headerEnd + 4);
                                if (bodyHave >= contentLen) break;
                            }
                        } else {
                            size_t bodyHave = raw.size() - (headerEnd + 4);
                            if (bodyHave >= contentLen) break;
                        }
                    }

                    auto req = parseHttpRequest(raw);
                    string resp = handle(req);
                    ::write(fd, resp.c_str(), resp.size());
                } catch (const exception& e) {
                    string body = string("{\"error\":") + toJsonString(e.what()) + "}";
                    string resp = httpResponse(500, body);
                    ::write(fd, resp.c_str(), resp.size());
                }
                close(fd);
            }).detach();
        }
    }
};

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string dbPath = "splitwise.db";
    int port = 8080;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) dbPath = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = stoi(argv[++i]);
    }

    try {
        Database db(dbPath);
        db.initSchema();
        SplitwiseService service(db);
        HttpServer server(port, service);
        server.run();
    } catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
