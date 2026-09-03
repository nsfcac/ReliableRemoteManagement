import psycopg
from app.config import settings

def connect():
    """ Connect to the PostgreSQL database server """
    conn = None
    try:
        # connect to the PostgreSQL server
        print('Connecting to the PostgreSQL database...')
        # autocommit=True so that "with conn.transaction():" opens a real
        # transaction that commits on exit. Without it, psycopg 3 starts an
        # implicit transaction on the first statement (the version query
        # below), every later transaction() becomes a savepoint nested inside
        # it, and the work is released but never committed. The connection
        # then sits "idle in transaction" and no write is ever durable.
        conn = psycopg.connect(settings.database_dsn, autocommit=True)

        # create a cursor
        cur = conn.cursor()

        # execute a statement
        print('PostgreSQL database version:')
        cur.execute('SELECT version()')

        # display the PostgreSQL database server version
        db_version = cur.fetchone()
        print(db_version)

        # close the communication with the PostgreSQL
        cur.close()

        return conn

    except Exception as error:
        raise RuntimeError(f"Error connecting to the database: {error}") from error
        

