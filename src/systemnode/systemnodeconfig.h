#ifndef SRC_SYSTEMNODECONFIG_H_
#define SRC_SYSTEMNODECONFIG_H_

#include <fs.h>

#include <string>
#include <vector>
#include <nodeconfig.h>

class CSystemnodeConfig;
extern CSystemnodeConfig systemnodeConfig;

class CSystemnodeConfig : public CNodeConfig
{
private:
    boost::filesystem::path getNodeConfigFile() override;
    std::string getHeader() override;
    std::string getFileName() override;
};

#endif /* SRC_SYSTEMNODECONFIG_H_ */
