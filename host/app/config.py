from pathlib import Path

from pydantic import SecretStr
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=Path(__file__).parent / ".env",   # find app/.env regardless of CWD
        env_file_encoding="utf-8",
        extra="ignore",           # ignore unrelated env vars
    )

    # Broker
    broker_host: str = "mosquitto"
    broker_port: int = 1883

    # Database
    db_host: str = "localhost"
    db_port: int = 5432
    db_user: str = "repacss"
    db_password: SecretStr = SecretStr("")   # masked in logs/repr
    db_name: str = "repacss"

    @property
    def database_dsn(self) -> str:
        return (
            f"postgresql://{self.db_user}:{self.db_password.get_secret_value()}"
            f"@{self.db_host}:{self.db_port}/{self.db_name}"
        )


settings = Settings()   # loaded once, at import