CREATE TABLE IF NOT EXISTS account_data (
  account_id BIGINT UNSIGNED NOT NULL,
  uid BIGINT UNSIGNED NOT NULL,
  account_name VARCHAR(128) DEFAULT NULL,
  open_id VARCHAR(128) DEFAULT NULL,
  state INT UNSIGNED NOT NULL,
  data_version BIGINT UNSIGNED NOT NULL,
  schema_version INT UNSIGNED NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  last_login_time_ms BIGINT UNSIGNED NOT NULL,
  last_logout_time_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  data_blob LONGBLOB NOT NULL,
  PRIMARY KEY (account_id),
  UNIQUE KEY uk_account_name (account_name),
  UNIQUE KEY uk_open_id (open_id),
  UNIQUE KEY uk_uid (uid),
  KEY idx_account_state (state),
  KEY idx_account_updated_at (updated_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS player_data (
  uid BIGINT UNSIGNED NOT NULL,
  account_id BIGINT UNSIGNED NOT NULL,
  state INT UNSIGNED NOT NULL,
  nickname VARCHAR(64) NOT NULL,
  level INT UNSIGNED NOT NULL,
  gold BIGINT NOT NULL,
  diamond BIGINT NOT NULL,
  room_id BIGINT UNSIGNED NOT NULL,
  data_version BIGINT UNSIGNED NOT NULL,
  schema_version INT UNSIGNED NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  last_login_time_ms BIGINT UNSIGNED NOT NULL,
  last_logout_time_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  data_blob LONGBLOB NOT NULL,
  PRIMARY KEY (uid),
  KEY idx_player_account_id (account_id),
  KEY idx_player_state (state),
  KEY idx_player_level (level),
  KEY idx_player_room_id (room_id),
  KEY idx_player_updated_at (updated_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS bag_data (
  uid BIGINT UNSIGNED NOT NULL,
  item_count INT UNSIGNED NOT NULL,
  equipment_count INT UNSIGNED NOT NULL,
  appearance_count INT UNSIGNED NOT NULL,
  capacity INT UNSIGNED NOT NULL,
  data_version BIGINT UNSIGNED NOT NULL,
  schema_version INT UNSIGNED NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  data_blob LONGBLOB NOT NULL,
  PRIMARY KEY (uid),
  KEY idx_bag_updated_at (updated_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS task_data (
  uid BIGINT UNSIGNED NOT NULL,
  active_count INT UNSIGNED NOT NULL,
  finished_count INT UNSIGNED NOT NULL,
  claimable_count INT UNSIGNED NOT NULL,
  data_version BIGINT UNSIGNED NOT NULL,
  schema_version INT UNSIGNED NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  data_blob LONGBLOB NOT NULL,
  PRIMARY KEY (uid),
  KEY idx_task_updated_at (updated_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS mail_data (
  mail_id BIGINT UNSIGNED NOT NULL,
  uid BIGINT UNSIGNED NOT NULL,
  state INT UNSIGNED NOT NULL,
  title VARCHAR(128) NOT NULL,
  sender VARCHAR(64) NOT NULL,
  send_time_ms BIGINT UNSIGNED NOT NULL,
  expire_time_ms BIGINT UNSIGNED NOT NULL,
  data_version BIGINT UNSIGNED NOT NULL,
  schema_version INT UNSIGNED NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  data_blob LONGBLOB NOT NULL,
  PRIMARY KEY (mail_id),
  KEY idx_mail_uid_state (uid, state),
  KEY idx_mail_uid_send_time (uid, send_time_ms),
  KEY idx_mail_expire_time (expire_time_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
