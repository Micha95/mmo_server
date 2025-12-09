#include "Player.h"
#include "../../common/include/MySQLClient.h"
#include <vector>
#include <string>
#include "ZoneManager.h"
#include "../../common/include/Log.h"

Player::Player() { type = EntityType::PLAYER; }

void Player::InitFromDB(MySQLClient& mysql, int32_t charId, const std::string& name, int32_t accountId) {
    id = charId;
    this->name = name;
    this->accountId = accountId;
    x = 0; y = 0; z = 0;
    std::string posQuery = "SELECT x, y, z FROM characters WHERE id=" + std::to_string(charId);
    std::vector<std::vector<std::string>> posResult;
    if (mysql.query(posQuery, posResult) && !posResult.empty() && posResult[0].size() == 3) {
        x = std::stof(posResult[0][0]);
        y = std::stof(posResult[0][1]);
        z = std::stof(posResult[0][2]);
    }
    ZoneManager* zm = ZoneManager::Get();
    zoneId = zm ? zm->CalculateZoneId(x, y) : 0;
}

void Player::Update(float deltaTime) {
    // TODO: Implement player-specific update logic (input, cooldowns, etc)
}

void Player::SaveToDB(MySQLClient& mysql) {
    // Save the player's current position to the characters table
    std::vector<std::pair<std::string, std::string>> fields = {
        {"x", std::to_string(x)},
        {"y", std::to_string(y)},
        {"z", std::to_string(z)}
    };
    std::vector<std::string> columns, values;
    for (const auto& f : fields) {
        columns.push_back(f.first);
        values.push_back(f.second);
    }
    std::string where = "id=" + std::to_string(id);
    if (!mysql.update("characters", columns, values, where)) {
        LOG_WARNING("Failed to save player " + std::to_string(id) + " to DB: x=" + std::to_string(x) + ", y=" + std::to_string(y) + ", z=" + std::to_string(z));
    }else{
        LOG_DEBUG("Player " + std::to_string(id) + " saved to DB: x=" + std::to_string(x) + ", y=" + std::to_string(y) + ", z=" + std::to_string(z));
    }
}
