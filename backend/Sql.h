#ifndef CEREVIA_SQL_H
#define CEREVIA_SQL_H

#include <sqlite3.h>

#include <string>

// ---------------------------------------------------------------------------
// Thin RAII + null-safety layer over the SQLite C API.
//
// The previous version called sqlite3_step() on statements that may have failed
// to prepare (a null pointer dereference) and built std::string directly from
// sqlite3_column_text(), which is undefined behaviour when the column is NULL.
// Every query in the backend now goes through these helpers instead.
// ---------------------------------------------------------------------------
namespace sql {

// Reads a TEXT column, substituting `fallback` when the value is NULL.
inline std::string text(sqlite3_stmt *stmt, int column, const std::string &fallback = "")
{
    if (!stmt) return fallback;
    const unsigned char *value = sqlite3_column_text(stmt, column);
    return value ? std::string(reinterpret_cast<const char *>(value)) : fallback;
}

inline int integer(sqlite3_stmt *stmt, int column, int fallback = 0)
{
    if (!stmt) return fallback;
    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return fallback;
    return sqlite3_column_int(stmt, column);
}

inline double real(sqlite3_stmt *stmt, int column, double fallback = 0.0)
{
    if (!stmt) return fallback;
    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return fallback;
    return sqlite3_column_double(stmt, column);
}

// Owns a prepared statement and finalizes it on every exit path.
class Stmt {
public:
    Stmt(sqlite3 *db, const char *query) : handle_(nullptr)
    {
        if (db && sqlite3_prepare_v2(db, query, -1, &handle_, nullptr) != SQLITE_OK) {
            handle_ = nullptr;
        }
    }

    ~Stmt() { if (handle_) sqlite3_finalize(handle_); }

    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;

    bool valid() const { return handle_ != nullptr; }
    sqlite3_stmt *get() const { return handle_; }
    explicit operator bool() const { return valid(); }

    Stmt &bind(int index, const std::string &value)
    {
        if (handle_) sqlite3_bind_text(handle_, index, value.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }

    Stmt &bind(int index, int value)
    {
        if (handle_) sqlite3_bind_int(handle_, index, value);
        return *this;
    }

    // True while another row is available.
    bool step() { return handle_ && sqlite3_step(handle_) == SQLITE_ROW; }

    // True when a write statement ran to completion.
    bool run() { return handle_ && sqlite3_step(handle_) == SQLITE_DONE; }

    std::string text(int column, const std::string &fallback = "") const
    {
        return sql::text(handle_, column, fallback);
    }

    int integer(int column, int fallback = 0) const { return sql::integer(handle_, column, fallback); }
    double real(int column, double fallback = 0.0) const { return sql::real(handle_, column, fallback); }

private:
    sqlite3_stmt *handle_;
};

} // namespace sql

#endif // CEREVIA_SQL_H
