add_library(funx-app-transaction-api INTERFACE)
target_include_directories(funx-app-transaction-api INTERFACE api)
target_link_libraries(funx-app-transaction-api INTERFACE funx-lib)

add_library(funx-app-transaction STATIC
    src/Database.cpp
    src/DatabaseSession.cpp
    src/FileStorage.cpp
)
target_include_directories(funx-app-transaction PUBLIC include)
target_link_libraries(funx-app-transaction PUBLIC
    funx-lib
    funx-app-transaction-api
)

add_funx_service(TransactionDbService transaction_db.cpp)
target_link_libraries(TransactionDbService PRIVATE
    funx-lib
    funx-app-transaction
)

add_library(funx-app-transaction-test INTERFACE)
target_include_directories(funx-app-transaction-test INTERFACE test)
target_link_libraries(funx-app-transaction-test INTERFACE
    funx-app-transaction
)

add_subdirectory(unit)

