-- Add passive mode flag to users table
ALTER TABLE users ADD COLUMN passive BOOLEAN NOT NULL DEFAULT FALSE;
CREATE INDEX idx_passive ON users(passive);
