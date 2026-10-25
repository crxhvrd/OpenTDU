#pragma once

class FilePathRegister {
public:
    FilePathRegister();
    ~FilePathRegister();

    void registerPath( const std::string &param_1 );
    void setDefaultPath( const std::string &param_1 );

    size_t getFiler( const char *param_1 );

    void mountUnit( const std::string &unitName, const std::string &folder );
    void unmountUnit( const std::string &unitName );
    bool resolveMountedUnit( std::string &path ) const;

private:
    struct PathEntry {
        std::string Path;
        size_t Priority;
    };

    // Emulates the Xbox 360 content roots ("radio0:\\") that
    // CXmlInstaller::AddRadioStation still builds paths for.
    struct MountedUnit {
        std::string Name;
        std::string Folder;
    };

private:
    std::vector<PathEntry> entries;
    std::vector<MountedUnit> mountedUnits;
    std::string defaultPath;
};

extern FilePathRegister gFilePathRegister;
