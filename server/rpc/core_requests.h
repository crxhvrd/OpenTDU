#pragma once

class Server;
class ActionService;

// coreresquests:: services (service discovery for now).
void RegisterCoreRequests( Server &server, ActionService &actions );
