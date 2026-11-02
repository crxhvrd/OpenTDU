#pragma once

#include <string>

struct sqlite3;

struct Account {
    uint32_t ID = 0u;
    std::string Nick;
    std::string Email;
    std::string PasswordHash;
    std::string PasswordSalt;
};

// SQLite storage of the server's persistent state (accounts for now).
class Database {
public:
    Database();
    ~Database();

    bool open( const char *pPath );
    void close();

    bool findAccountByNick( const std::string &nick, Account &outAccount );
    bool findAccountByID( const uint32_t id, Account &outAccount );
    bool createAccount( const std::string &nick,
                        const std::string &email,
                        const std::string &password,
                        const std::string &cdKey,
                        Account &outAccount );
    void touchAccountLogin( const uint32_t id );

    static bool CheckPassword( const Account &account,
                               const std::string &password );

private:
    sqlite3 *pDatabase;

private:
    bool execute( const char *pSQL );
    bool migrate();
};
