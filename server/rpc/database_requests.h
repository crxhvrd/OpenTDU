#pragma once

class Server;
class ActionService;

// Database:: (login), Stats:: and Savegame:: requests sent during login.
void RegisterDatabaseRequests( Server &server, ActionService &actions );
