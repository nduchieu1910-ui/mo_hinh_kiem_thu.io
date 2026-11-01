#pragma once
// =====================================================================
//  AdapterBase — phần dùng chung của mọi adapter: quản lý người nghe
//  sự kiện (listenEvent) và phát sự kiện sau mỗi giao dịch thành công.
// =====================================================================
#include "cc/adapter/IBlockchainAdapter.h"

#include <map>
#include <string>
#include <vector>

namespace cc {

class AdapterBase : public IBlockchainAdapter {
public:
    // eventName = "*" để nghe mọi sự kiện.
    void listenEvent(const std::string& eventName, EventCallback callback) override {
        listeners_[eventName].push_back(std::move(callback));
    }

protected:
    void emit(const std::vector<Event>& events) const {
        for (const Event& e : events) {
            if (auto it = listeners_.find(e.name); it != listeners_.end())
                for (const auto& cb : it->second) cb(e);
            if (auto it = listeners_.find("*"); it != listeners_.end())
                for (const auto& cb : it->second) cb(e);
        }
    }

private:
    std::map<std::string, std::vector<EventCallback>> listeners_;
};

} // namespace cc
