#pragma once

#include <string>
#include <vector>

// Network tunables oOverridable by the server through netconfig.xml
class edNetConfig {
public:
    struct Variable {
        const char *pName;
        double Value;
        double DefaultValue;
    };

public:
    inline const std::vector<Variable> &getVariables() const {
        return variables;
    }

public:
    edNetConfig();

    bool parse( const char *pXml );
    std::string toXml() const;
    void resetDefaults();

    int32_t getInt( const char *pName ) const;
    float getFloat( const char *pName ) const;

private:
    std::vector<Variable> variables;

private:
    const Variable *find( const char *pName ) const;
};

extern edNetConfig gNetConfig;
