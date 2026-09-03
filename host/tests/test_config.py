"""Tests for app.config.Settings.

Cases:
- database_dsn assembles a correct postgresql:// URL from the parts.
- The DB password is a SecretStr: masked in repr/str, real value only on request.
- A string port is validated and coerced to int (env vars arrive as strings).
"""

from app.config import Settings


def test_database_dsn_builds_expected_url():
    s = Settings(db_user="u", db_password="p", db_host="h", db_port=1234, db_name="d")
    assert s.database_dsn == "postgresql://u:p@h:1234/d"


def test_password_is_not_leaked_in_repr():
    s = Settings(db_user="u", db_password="supersecret", db_host="h", db_port=1, db_name="d")
    assert "supersecret" not in repr(s)               # not in the model repr
    assert "supersecret" not in str(s.db_password)    # SecretStr masks as **********
    assert s.db_password.get_secret_value() == "supersecret"  # real value on demand


def test_broker_port_coerced_to_int():
    s = Settings(broker_port="1883")
    assert s.broker_port == 1883
    assert isinstance(s.broker_port, int)
