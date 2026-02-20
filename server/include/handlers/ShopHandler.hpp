#ifndef MU_SHOP_HANDLER_HPP
#define MU_SHOP_HANDLER_HPP

#include "Database.hpp"
#include "Session.hpp"
#include <vector>

namespace ShopHandler {
void HandleShopOpen(Session &session, const std::vector<uint8_t> &packet,
                    Database &db);
void HandleShopBuy(Session &session, const std::vector<uint8_t> &packet,
                   Database &db);
void HandleShopSell(Session &session, const std::vector<uint8_t> &packet,
                    Database &db);
} // namespace ShopHandler

#endif // MU_SHOP_HANDLER_HPP
