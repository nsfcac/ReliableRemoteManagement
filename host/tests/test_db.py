"""Tests for app.db.connect.

Cases:
- connect() opens a psycopg connection using settings.database_dsn, with autocommit
  enabled so that transaction() blocks commit, and returns it.
- A driver error is wrapped in a RuntimeError (chained), never swallowed.

psycopg.connect is patched, so no real database is touched.
"""

from unittest.mock import MagicMock, patch

import pytest

import app.db as db


def test_connect_uses_dsn_and_returns_connection():
    fake_conn = MagicMock(name="conn")
    with patch("app.db.psycopg.connect", return_value=fake_conn) as mock_connect:
        result = db.connect()
    assert result is fake_conn
    # autocommit is required: without it the version query below opens an
    # implicit transaction, every later conn.transaction() becomes a savepoint
    # nested inside it, and no write is ever committed.
    mock_connect.assert_called_once_with(db.settings.database_dsn, autocommit=True)


def test_connect_raises_runtimeerror_on_failure():
    with patch("app.db.psycopg.connect", side_effect=Exception("boom")):
        with pytest.raises(RuntimeError):
            db.connect()
