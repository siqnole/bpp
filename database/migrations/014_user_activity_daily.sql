-- ═══════════════════════════════════════════════════════════════════════════════
-- migration 014: user_activity_daily — pre-aggregated per-user daily counters
-- enables fast leaderboard queries for messages, vc time, and commands
-- without scanning raw event tables or doing expensive self-joins
-- ═══════════════════════════════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS user_activity_daily (
    guild_id        VARCHAR(20)  NOT NULL,
    user_id         VARCHAR(20)  NOT NULL,
    stat_date       DATE         NOT NULL,
    messages        INT          NOT NULL DEFAULT 0,
    edits           INT          NOT NULL DEFAULT 0,
    deletes         INT          NOT NULL DEFAULT 0,
    voice_minutes   INT          NOT NULL DEFAULT 0,
    commands_used   INT          NOT NULL DEFAULT 0,

    PRIMARY KEY (guild_id, user_id, stat_date),
    INDEX idx_uad_guild_date       (guild_id, stat_date),
    INDEX idx_uad_guild_user       (guild_id, user_id),
    INDEX idx_uad_guild_date_msgs  (guild_id, stat_date, messages DESC),
    INDEX idx_uad_guild_date_vc    (guild_id, stat_date, voice_minutes DESC),
    INDEX idx_uad_guild_date_cmds  (guild_id, stat_date, commands_used DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;


-- ═══════════════════════════════════════════════════════════════════════════════
-- backfill: populate user_activity_daily from existing event tables
-- run this ONCE after creating the table to import historical data
-- ═══════════════════════════════════════════════════════════════════════════════

-- backfill messages / edits / deletes from guild_message_events
INSERT INTO user_activity_daily (guild_id, user_id, stat_date, messages, edits, deletes)
SELECT
    guild_id,
    user_id,
    DATE(created_at) AS stat_date,
    SUM(event_type = 'message')  AS messages,
    SUM(event_type = 'edit')     AS edits,
    SUM(event_type = 'delete')   AS deletes
FROM guild_message_events
WHERE user_id != '0'
GROUP BY guild_id, user_id, DATE(created_at)
ON DUPLICATE KEY UPDATE
    messages = messages + VALUES(messages),
    edits    = edits    + VALUES(edits),
    deletes  = deletes  + VALUES(deletes);

-- backfill commands_used from guild_command_usage
-- note: guild_command_usage doesn't track per-user, so we fall back to
-- command_stats if it exists (per-event logging with user_id)
INSERT INTO user_activity_daily (guild_id, user_id, stat_date, commands_used)
SELECT
    guild_id,
    user_id,
    DATE(used_at) AS stat_date,
    COUNT(*)      AS commands_used
FROM command_stats
WHERE guild_id IS NOT NULL AND guild_id != ''
GROUP BY guild_id, user_id, DATE(used_at)
ON DUPLICATE KEY UPDATE
    commands_used = commands_used + VALUES(commands_used);

-- backfill voice_minutes from paired join/leave events in guild_voice_events
-- this uses the same TIMESTAMPDIFF self-join pattern as the existing queries
INSERT INTO user_activity_daily (guild_id, user_id, stat_date, voice_minutes)
SELECT
    paired.guild_id,
    paired.user_id,
    paired.stat_date,
    COALESCE(SUM(paired.dur_min), 0) AS voice_minutes
FROM (
    SELECT
        j.guild_id,
        j.user_id,
        DATE(j.created_at) AS stat_date,
        TIMESTAMPDIFF(MINUTE, j.created_at, MIN(l.created_at)) AS dur_min
    FROM guild_voice_events j
    INNER JOIN guild_voice_events l
        ON  l.guild_id   = j.guild_id
        AND l.user_id    = j.user_id
        AND l.channel_id = j.channel_id
        AND l.event_type = 'leave'
        AND l.created_at > j.created_at
    WHERE j.event_type = 'join'
    GROUP BY j.id, j.guild_id, j.user_id, DATE(j.created_at), j.created_at
) paired
WHERE paired.dur_min > 0 AND paired.dur_min < 1440
GROUP BY paired.guild_id, paired.user_id, paired.stat_date
ON DUPLICATE KEY UPDATE
    voice_minutes = voice_minutes + VALUES(voice_minutes);
