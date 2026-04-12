// zephyr_nvs.hpp -- INvsStorage implementation for Zephyr NVS
#ifndef ZEPHYR_NVS_HPP
#define ZEPHYR_NVS_HPP

#include "beacon_config.hpp"

#ifndef HOST_TEST
#include <zephyr/fs/nvs.h>
#endif

namespace beacon {

class ZephyrNvsStorage : public INvsStorage {
  public:
    /// Mount the NVS partition. Returns 0 on success.
    int init();

    int read(uint16_t id, void* data, size_t len) override;
    int write(uint16_t id, const void* data, size_t len) override;

  private:
#ifndef HOST_TEST
    struct nvs_fs fs_;
#endif
    bool mounted_ = false;
};

} // namespace beacon

#endif // ZEPHYR_NVS_HPP
