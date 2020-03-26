"""
    dvs_database contains utilities for interacting with redis when writing
    tests for the virtual switch.
"""
from swsscommon import swsscommon
from dvslib.dvs_common import wait_for_result, PollingConfig


class DVSDatabase(object):
    """
        DVSDatabase provides access to redis databases on the virtual switch.

        By default, database operations are configured to use
        `DEFAULT_POLLING_CONFIG`. Users can specify their own PollingConfig,
        but this shouldn't typically be necessary.
    """
    DEFAULT_POLLING_CONFIG = PollingConfig(polling_interval=0.01, timeout=5, strict=True)

    def __init__(self, db_id, connector):
        """
            Initializes a DVSDatabase instance.

            Args:
                db_id (int): The integer ID used to identify the given database
                    instance in redis.
                connector (str): The I/O connection used to communicate with
                    redis (e.g. unix socket, tcp socket, etc.).
        """

        self.db_connection = swsscommon.DBConnector(db_id, connector, 0)

    def create_entry(self, table_name, key, entry):
        """
            Adds the mapping {`key` -> `entry`} to the specified table.

            Args:
                table_name (str): The name of the table to add the entry to.
                key (str): The key that maps to the entry.
                entry (Dict[str, str]): A set of key-value pairs to be stored.
        """

        table = swsscommon.Table(self.db_connection, table_name)
        formatted_entry = swsscommon.FieldValuePairs(entry.items())
        table.set(key, formatted_entry)

    def get_entry(self, table_name, key):
        """
            Gets the entry stored at `key` in the specified table.

            Args:
                table_name (str): The name of the table where the entry is
                    stored.
                key (str): The key that maps to the entry being retrieved.

            Returns:
                Dict[str, str]: The entry stored at `key`. If no entry is found,
                then an empty Dict will be returned.
        """

        table = swsscommon.Table(self.db_connection, table_name)
        (status, fv_pairs) = table.get(key)

        if not status:
            return {}

        return dict(fv_pairs)

    def delete_entry(self, table_name, key):
        """
            Removes the entry stored at `key` in the specified table.

            Args:
                table_name (str): The name of the table where the entry is
                    being removed.
                key (str): The key that maps to the entry being removed.
        """

        table = swsscommon.Table(self.db_connection, table_name)
        table._del(key)  # pylint: disable=protected-access

    def get_keys(self, table_name):
        """
            Gets all of the keys stored in the specified table.

            Args:
                table_name (str): The name of the table from which to fetch
                the keys.

            Returns:
                List[str]: The keys stored in the table. If no keys are found,
                then an empty List will be returned.
        """

        table = swsscommon.Table(self.db_connection, table_name)
        keys = table.getKeys()

        return keys if keys else []

    def wait_for_entry(self, table_name, key,
                       polling_config=DEFAULT_POLLING_CONFIG):
        """
            Gets the entry stored at `key` in the specified table. This method
            will wait for the entry to exist.

            Args:
                table_name (str): The name of the table where the entry is
                    stored.
                key (str): The key that maps to the entry being retrieved.
                polling_config (PollingConfig): The parameters to use to poll
                    the db.

            Returns:
                Dict[str, str]: The entry stored at `key`. If no entry is found,
                then an empty Dict will be returned.

        """

        def _access_function():
            fv_pairs = self.get_entry(table_name, key)
            return (bool(fv_pairs), fv_pairs)

        return wait_for_result(_access_function, polling_config)

    def wait_for_empty_entry(self,
                             table_name,
                             key,
                             polling_config=DEFAULT_POLLING_CONFIG):
        """
            Checks if there is any entry stored at `key` in the specified
            table. This method will wait for the entry to be empty.

            Args:
                table_name (str): The name of the table being checked.
                key (str): The key to be checked.
                polling_config (PollingConfig): The parameters to use to poll
                    the db.

            Returns:
                bool: True if no entry exists at `key`, False otherwise.
        """

        def _access_function():
            fv_pairs = self.get_entry(table_name, key)
            return (not fv_pairs, fv_pairs)

        return wait_for_result(_access_function, polling_config)

    def wait_for_n_keys(self,
                        table_name,
                        num_keys,
                        polling_config=DEFAULT_POLLING_CONFIG):
        """
            Gets all of the keys stored in the specified table. This method
            will wait for the specified number of keys.

            Args:
                table_name (str): The name of the table from which to fetch
                    the keys.
                num_keys (int): The expected number of keys to retrieve from
                    the table.
                polling_config (PollingConfig): The parameters to use to poll
                    the db.

            Returns:
                List[str]: The keys stored in the table. If no keys are found,
                then an empty List will be returned.
        """

        def _access_function():
            keys = self.get_keys(table_name)
            return (len(keys) == num_keys, keys)

        return wait_for_result(_access_function, polling_config)
