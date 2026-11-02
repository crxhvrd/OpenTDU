#include "shared.h"
#include "database.h"

#include "core/hash/sha256.h"

#include <sqlite3.h>

namespace {
    // RAII wrapper around a prepared statement.
    class Statement {
    public:
        Statement( sqlite3 *pDatabase, const char *pSQL )
            : pStatement( nullptr ) {
            if ( sqlite3_prepare_v2( pDatabase, pSQL, -1, &pStatement,
                                     nullptr ) != SQLITE_OK ) {
                OTDU_LOG_ERROR( "SQL prepare failed (%s): %s\n",
                                sqlite3_errmsg( pDatabase ), pSQL );
                pStatement = nullptr;
            }
        }
        ~Statement() { sqlite3_finalize( pStatement ); }

        inline bool isValid() const { return pStatement != nullptr; }
        inline sqlite3_stmt *get() const { return pStatement; }

        void bind( const int32_t index, const std::string &value ) {
            sqlite3_bind_text( pStatement, index, value.c_str(),
                               int32_t( value.size() ), SQLITE_TRANSIENT );
        }
        void bind( const int32_t index, const int64_t value ) {
            sqlite3_bind_int64( pStatement, index, value );
        }

        std::string columnText( const int32_t index ) const {
            const unsigned char *pText =
                sqlite3_column_text( pStatement, index );
            return pText != nullptr ? reinterpret_cast<const char *>( pText )
                                    : "";
        }

    private:
        sqlite3_stmt *pStatement;
    };

    void ReadAccount( const Statement &statement, Account &outAccount ) {
        outAccount.ID = uint32_t( sqlite3_column_int64( statement.get(), 0 ) );
        outAccount.Nick = statement.columnText( 1 );
        outAccount.Email = statement.columnText( 2 );
        outAccount.PasswordHash = statement.columnText( 3 );
        outAccount.PasswordSalt = statement.columnText( 4 );
    }
} // namespace

Database::Database() : pDatabase( nullptr ) {}

Database::~Database() { close(); }

bool Database::open( const char *pPath ) {
    close();

    if ( sqlite3_open( pPath, &pDatabase ) != SQLITE_OK ) {
        OTDU_LOG_ERROR( "Failed to open database '%s' (%s)\n", pPath,
                        sqlite3_errmsg( pDatabase ) );
        close();
        return false;
    }

    execute( "PRAGMA journal_mode=WAL;" );
    execute( "PRAGMA foreign_keys=ON;" );
    return migrate();
}

void Database::close() {
    if ( pDatabase != nullptr ) {
        sqlite3_close( pDatabase );
        pDatabase = nullptr;
    }
}

bool Database::execute( const char *pSQL ) {
    char *pError = nullptr;
    if ( sqlite3_exec( pDatabase, pSQL, nullptr, nullptr, &pError ) !=
         SQLITE_OK ) {
        OTDU_LOG_ERROR( "SQL error (%s): %s\n",
                        pError != nullptr ? pError : "?", pSQL );
        sqlite3_free( pError );
        return false;
    }
    return true;
}

bool Database::migrate() {
    // Account ids start at 1: 0 means "no profile" on the client.
    return execute( "CREATE TABLE IF NOT EXISTS accounts ("
                    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "  nick TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                    "  email TEXT NOT NULL DEFAULT '',"
                    "  password_hash TEXT NOT NULL,"
                    "  password_salt TEXT NOT NULL,"
                    "  cdkey_hash TEXT NOT NULL DEFAULT '',"
                    "  created_at INTEGER NOT NULL DEFAULT (strftime('%s')),"
                    "  last_login_at INTEGER"
                    ");" );
}

bool Database::findAccountByNick( const std::string &nick,
                                  Account &outAccount ) {
    Statement statement( pDatabase,
                         "SELECT id, nick, email, password_hash, "
                         "password_salt FROM accounts WHERE nick = ?1;" );
    if ( !statement.isValid() ) {
        return false;
    }
    statement.bind( 1, nick );
    if ( sqlite3_step( statement.get() ) != SQLITE_ROW ) {
        return false;
    }
    ReadAccount( statement, outAccount );
    return true;
}

bool Database::findAccountByID( const uint32_t id, Account &outAccount ) {
    Statement statement( pDatabase,
                         "SELECT id, nick, email, password_hash, "
                         "password_salt FROM accounts WHERE id = ?1;" );
    if ( !statement.isValid() ) {
        return false;
    }
    statement.bind( 1, int64_t( id ) );
    if ( sqlite3_step( statement.get() ) != SQLITE_ROW ) {
        return false;
    }
    ReadAccount( statement, outAccount );
    return true;
}

bool Database::createAccount( const std::string &nick,
                              const std::string &email,
                              const std::string &password,
                              const std::string &cdKey,
                              Account &outAccount ) {
    const std::string salt = GenerateSaltHex();
    const std::string hash = HashPassword( password, salt );
    const SHA256Digest_t cdKeyDigest = SHA256( cdKey.data(), cdKey.size() );

    Statement statement(
        pDatabase, "INSERT INTO accounts (nick, email, password_hash, "
                   "password_salt, cdkey_hash) VALUES (?1, ?2, ?3, ?4, ?5);" );
    if ( !statement.isValid() ) {
        return false;
    }
    statement.bind( 1, nick );
    statement.bind( 2, email );
    statement.bind( 3, hash );
    statement.bind( 4, salt );
    statement.bind( 5, ToHexString( cdKeyDigest.data(), cdKeyDigest.size() ) );
    if ( sqlite3_step( statement.get() ) != SQLITE_DONE ) {
        return false;
    }

    outAccount.ID = uint32_t( sqlite3_last_insert_rowid( pDatabase ) );
    outAccount.Nick = nick;
    outAccount.Email = email;
    outAccount.PasswordHash = hash;
    outAccount.PasswordSalt = salt;
    return true;
}

void Database::touchAccountLogin( const uint32_t id ) {
    Statement statement( pDatabase, "UPDATE accounts SET last_login_at = "
                                    "strftime('%s') WHERE id = ?1;" );
    if ( statement.isValid() ) {
        statement.bind( 1, int64_t( id ) );
        sqlite3_step( statement.get() );
    }
}

bool Database::CheckPassword( const Account &account,
                              const std::string &password ) {
    return HashPassword( password, account.PasswordSalt ) ==
           account.PasswordHash;
}
