-- ═══════════════════════════════════════════════════════════════════════════════
-- migration 011: guild stats tracking
-- tracks member joins/leaves, messages/edits/deletes, command usage per guild
-- individual events retained ~7 days, daily rollups kept indefinitely
-- ═══════════════════════════════════════════════════════════════════════════════

-- individual member join/leave events
CREATE TABLE IF NOT EXISTS guild_member_events (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    guild_id    VARCHAR(20) NOT NULL,
    user_id     VARCHAR(20) NOT NULL,
    event_type  ENUM('join', 'leave') NOT NULL,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_gme_guild_time (guild_id, created_at),
    INDEX idx_gme_guild_type_time (guild_id, event_type, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- individual message / edit / delete events (purged after 7 days)
CREATE TABLE IF NOT EXISTS guild_message_events (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    guild_id    VARCHAR(20) NOT NULL,
    user_id     VARCHAR(20) NOT NULL DEFAULT '0',
    channel_id  VARCHAR(20) NOT NULL,
    event_type  ENUM('message', 'edit', 'delete') NOT NULL,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_gmsge_guild_time (guild_id, created_at),
    INDEX idx_gmsge_guild_chan_time (guild_id, channel_id, created_at),
    INDEX idx_gmsge_guild_user_time (guild_id, user_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- daily rollup — one row per guild (+optional channel breakdown) per day
-- channel_id = '__guild__' for the guild-wide aggregate row
CREATE TABLE IF NOT EXISTS guild_daily_stats (
    id              BIGINT AUTO_INCREMENT PRIMARY KEY,
    guild_id        VARCHAR(20) NOT NULL,
    stat_date       DATE NOT NULL,
    channel_id      VARCHAR(20) NOT NULL DEFAULT '__guild__',
    messages_count  INT NOT NULL DEFAULT 0,
    edits_count     INT NOT NULL DEFAULT 0,
    deletes_count   INT NOT NULL DEFAULT 0,
    joins_count     INT NOT NULL DEFAULT 0,
    leaves_count    INT NOT NULL DEFAULT 0,
    commands_count  INT NOT NULL DEFAULT 0,
    active_users    INT NOT NULL DEFAULT 0,

    UNIQUE KEY uk_gds_guild_date_chan (guild_id, stat_date, channel_id),
    INDEX idx_gds_guild_date (guild_id, stat_date)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- per-command per-channel daily usage counters
CREATE TABLE IF NOT EXISTS guild_command_usage (
    id              BIGINT AUTO_INCREMENT PRIMARY KEY,
    guild_id        VARCHAR(20) NOT NULL,
    command_name    VARCHAR(64) NOT NULL,
    channel_id      VARCHAR(20) NOT NULL,
    usage_date      DATE NOT NULL,
    use_count       INT NOT NULL DEFAULT 1,

    UNIQUE KEY uk_gcu_guild_cmd_chan_date (guild_id, command_name, channel_id, usage_date),
    INDEX idx_gcu_guild_date (guild_id, usage_date),
    INDEX idx_gcu_guild_cmd_date (guild_id, command_name, usage_date)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ═══════════════════════════════════════════════════════════════════════════════
-- scheduled cleanup: purge guild_message_events older than 7 days
-- run this via cron, mysql event scheduler, or the node server's daily rollup
-- ═══════════════════════════════════════════════════════════════════════════════
-- DELETE FROM guild_message_events WHERE created_at < DATE_SUB(NOW(), INTERVAL 7 DAY);
