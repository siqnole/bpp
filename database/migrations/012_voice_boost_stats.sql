-- ═══════════════════════════════════════════════════════════════════════════════
-- migration 012: voice activity + boost tracking
-- tracks voice channel join/leave sessions and server boost events
-- ═══════════════════════════════════════════════════════════════════════════════

-- individual voice channel events (join / leave)
CREATE TABLE IF NOT EXISTS guild_voice_events (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    guild_id    VARCHAR(20) NOT NULL,
    user_id     VARCHAR(20) NOT NULL,
    channel_id  VARCHAR(20) NOT NULL,
    event_type  ENUM('join', 'leave') NOT NULL,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_gve_guild_time (guild_id, created_at),
    INDEX idx_gve_guild_chan_time (guild_id, channel_id, created_at),
    INDEX idx_gve_guild_user_time (guild_id, user_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- server boost events (boost / unboost)
CREATE TABLE IF NOT EXISTS guild_boost_events (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    guild_id    VARCHAR(20) NOT NULL,
    user_id     VARCHAR(20) NOT NULL,
    event_type  ENUM('boost', 'unboost') NOT NULL,
    boost_id    VARCHAR(32) NOT NULL DEFAULT '',
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_gbe_guild_time (guild_id, created_at),
    INDEX idx_gbe_guild_user_time (guild_id, user_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
