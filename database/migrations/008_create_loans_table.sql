-- Create loans table for bank upgrade financing system
CREATE TABLE IF NOT EXISTS loans (
    user_id BIGINT UNSIGNED PRIMARY KEY,
    principal BIGINT NOT NULL,
    interest BIGINT NOT NULL,
    remaining BIGINT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_payment_at TIMESTAMP NULL,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
