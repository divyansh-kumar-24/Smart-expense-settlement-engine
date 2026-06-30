
# Smart Expense Settlement Engine (C++ Backend)

> A backend-only expense settlement platform inspired by Splitwise, implemented in modern C++ with SQLite and a custom multithreaded REST server.

## Overview

Smart Expense Settlement Engine is a backend system that enables users to create groups, record shared expenses, split bills using multiple strategies, compute net balances, and generate an optimized settlement plan that minimizes the number of transactions.

The project demonstrates backend system design, object-oriented programming, REST API implementation without external web frameworks, SQLite persistence, transaction management, multithreading, and graph-based debt optimization.

---

# Features

- User management
- Group creation
- Group membership management
- Expense recording
- Equal split
- Exact split
- Percentage split
- Automatic balance calculation
- Debt simplification
- SQLite persistence
- Multithreaded HTTP server
- JSON request/response handling
- Transaction rollback support
- Input validation
- REST APIs

---

# Technology Stack

| Component | Technology |
|-----------|------------|
| Language | C++17 |
| Database | SQLite3 |
| Server | POSIX Socket Programming |
| API | REST |
| Concurrency | std::thread |
| Build | g++ |

---

# Project Structure

```text
Smart-Expense-Settlement-Engine/
│
├── main.cpp
├── database_schema.sql
├── README.md
```

---

# Architecture

```text
Client (Postman)
        │
        ▼
REST HTTP Server
        │
        ▼
Service Layer
        │
        ▼
Repositories
        │
        ▼
SQLite Database
```

---

# Implemented REST APIs

| Method | Endpoint |
|--------|----------|
| GET | /health |
| GET | /users |
| POST | /users |
| POST | /groups |
| POST | /groups/members |
| POST | /expenses |
| GET | /groups/{id}/balances |
| GET | /groups/{id}/settlements |

---

# Build Instructions

## Linux

Install SQLite:

```bash
sudo apt install sqlite3 libsqlite3-dev
```

Compile:

```bash
g++ -std=c++17 main.cpp -lsqlite3 -lpthread -o splitwise
```

Run:

```bash
./splitwise
```

Server starts on:

```text
http://localhost:8080
```

---

# Windows (MinGW)

```bash
g++ -std=c++17 main.cpp -lsqlite3 -lws2_32 -o splitwise.exe
splitwise.exe
```

---

# Testing Procedure

The backend was tested without a frontend.

1. Compile the backend.
2. Start the executable.
3. Verify `/health`.
4. Use Postman to invoke every API.
5. Verify returned JSON.
6. Inspect SQLite database.
7. Test invalid requests.

---

# Example Requests

## Create User

POST `/users`

```json
{
  "name":"Alice",
  "email":"alice@gmail.com"
}
```

Response

```json
{
  "id":1
}
```

---

## Create Group

```json
{
"name":"Trip",
"owner_id":1
}
```

---

## Add Member

```json
{
"group_id":1,
"user_id":2
}
```

---

## Add Equal Expense

```json
{
"group_id":1,
"paid_by":1,
"amount":1200,
"note":"Dinner",
"split_type":"equal"
}
```

---

## View Balances

GET

```text
/groups/1/balances
```

---

## View Settlements

GET

```text
/groups/1/settlements
```

---

# Database

Tables

- users
- groups_table
- group_members
- expenses
- expense_splits

SQLite is initialized automatically if the database does not exist.

---

# Debt Simplification

Net balances are computed from all recorded expense splits.

Positive balances represent creditors.

Negative balances represent debtors.

A greedy priority-queue based settlement algorithm minimizes the number of transactions.

Time Complexity:

- Ledger Construction: O(E)
- Settlement: O(N log N)

---

# OOP Concepts

- Encapsulation
- Abstraction
- Composition
- Repository Pattern
- Service Layer
- Separation of Concerns

---

# Error Handling

The backend validates:

- Invalid user IDs
- Invalid group IDs
- Negative expense amounts
- Invalid split totals
- Percentage sums not equal to 100
- Missing members
- Duplicate memberships

Database writes are wrapped in SQL transactions with rollback support.

---

# How I Tested This Project

The backend server was compiled using g++ and executed from the terminal.

All REST APIs were tested using Postman.

The SQLite database was inspected after every major operation to verify persistence and correctness.

Edge cases such as invalid users, invalid groups, incorrect split totals, duplicate members, and malformed requests were also tested.

---

# Future Improvements

- JWT Authentication
- OAuth Login
- Docker Support
- PostgreSQL
- Redis Cache
- Unit Tests
- CI/CD Pipeline
- Web Frontend
- Mobile Application

---

# Resume Summary

Developed a backend expense settlement platform in C++ implementing a custom multithreaded REST server, SQLite persistence, transaction management, multiple bill-splitting strategies, and graph-based debt optimization.

---

# License

MIT License.
