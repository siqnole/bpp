-- Migration: create guild_role_classes and guild_role_class_members
-- Run this against your bronxbot database

CREATE TABLE IF NOT EXISTS guild_role_classes (
    id              INT UNSIGNED    NOT NULL AUTO_INCREMENT,
    guild_id        VARCHAR(20)     NOT NULL,
    name            VARCHAR(100)    NOT NULL,
    priority        INT             NOT NULL DEFAULT 0,
    inherit_lower   TINYINT(1)      NOT NULL DEFAULT 0,
    restrictions    JSON                     DEFAULT NULL,
    created_at      DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    INDEX idx_guild_id (guild_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS guild_role_class_members (
    id          INT UNSIGNED    NOT NULL AUTO_INCREMENT,
    guild_id    VARCHAR(20)     NOT NULL,
    role_id     VARCHAR(20)     NOT NULL,
    class_id    INT UNSIGNED    NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uq_guild_role (guild_id, role_id),
    INDEX idx_class_id (class_id),
    CONSTRAINT fk_rcm_class
        FOREIGN KEY (class_id)
        REFERENCES guild_role_classes (id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;