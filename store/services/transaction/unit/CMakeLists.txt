add_catch_test(test_transaction_file_storage
    SOURCES
        test_transaction_file_storage.cpp
    LIBRARIES
        funx-app-transaction-test
)

add_catch_test(test_transaction_database
    SOURCES
        test_transaction_database.cpp
    LIBRARIES
        funx-app-transaction-test
)

