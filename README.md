Smart Expense Settlement Engine (C++ + SQLite)


## Overview

Splitwise Clone is a backend system developed in C++ that allows users to create groups, record expenses, split bills among participants, and minimize debt transactions.

The primary objective of this project is to demonstrate:

* Object-Oriented Programming (OOP)
* Backend System Design
* REST API Design
* SQL Database Integration
* Graph Algorithms
* Data Structures & Algorithms
* Software Engineering Principles

The system replicates the core functionality of Splitwise while maintaining clean architecture and scalability.

---

# Features

## User Management

Users can:

* Register
* Retrieve profile information
* Participate in multiple groups
* Create expenses
* View balances

### APIs

```http
POST /users
GET /users/{id}
```

---

## Group Management

Users can create groups and add members.

Examples:

* Flatmates
* College Trip
* Office Team Lunch
* Family Expenses

### APIs

```http
POST /groups
POST /groups/members
GET /groups/{id}
```

---

## Expense Management

Users can create expenses within groups.

Each expense stores:

* Expense description
* Total amount
* Paid by user
* Participants
* Split strategy

### APIs

```http
POST /expenses
GET /expenses/{id}
```

---

## Supported Split Types

### 1. Equal Split

Example:

Amount = ₹1200

Users:

* A
* B
* C

Each owes:

₹400

---

### 2. Exact Split

Example:

Amount = ₹1000

Users:

* A → ₹500
* B → ₹300
* C → ₹200

System validates:

```text
sum(split amounts) = total expense
```

---

### 3. Percentage Split

Example:

Amount = ₹2000

Users:

* A → 50%
* B → 30%
* C → 20%

Calculated:

```text
A = 1000
B = 600
C = 400
```

System validates:

```text
sum(percentages) = 100
```

---

# Debt Simplification

This is the most important feature.

Without simplification:

```text
A owes B = 500
B owes C = 500
```

Transactions = 2

After simplification:

```text
A owes C = 500
```

Transactions = 1

The objective is to minimize the number of settlements.

---

# System Architecture

```text
Client
   |
   v
REST API Layer
   |
   v
Service Layer
   |
   v
Business Logic Layer
   |
   +---- Expense Engine
   |
   +---- Debt Simplifier
   |
   +---- Validation Engine
   |
   v
Repository Layer
   |
   v
SQLite Database
```

---

# High Level Workflow

## Creating Expense

```text
User Creates Expense
        |
        v
Validate Request
        |
        v
Determine Split Type
        |
        v
Generate Balances
        |
        v
Store Expense
        |
        v
Update Debt Graph
```

---

## Simplifying Debt

```text
Read Group Balances
        |
        v
Construct Graph
        |
        v
Compute Net Amounts
        |
        v
Apply Greedy Settlement
        |
        v
Generate Minimal Transactions
```

---

# Database Design

SQLite is used as the persistent storage layer.

---

## Users Table

```sql
Users
(
    user_id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
)
```

Purpose:

Stores registered users.

---

## Groups Table

```sql
Groups
(
    group_id INTEGER PRIMARY KEY,
    name TEXT
)
```

Purpose:

Stores expense groups.

---

## GroupMembers Table

```sql
GroupMembers
(
    group_id,
    user_id
)
```

Purpose:

Maintains many-to-many relation between groups and users.

---

## Expenses Table

```sql
Expenses
(
    expense_id,
    group_id,
    paid_by,
    amount,
    description
)
```

Purpose:

Stores all expenses.

---

## ExpenseSplits Table

```sql
ExpenseSplits
(
    expense_id,
    user_id,
    owed_amount
)
```

Purpose:

Stores individual user shares.

---

# OOP Design

The project follows object-oriented design principles.

---

# Core Classes

---

## User

Represents a system user.

Responsibilities:

* User identity
* Profile information

Attributes:

```cpp
class User
{
    int id;
    string name;
    string email;
};
```

---

## Group

Represents an expense group.

Responsibilities:

* Group creation
* Member management

Attributes:

```cpp
class Group
{
    int id;
    string name;
    vector<User*> members;
};
```

---

## Expense

Represents a financial transaction.

Responsibilities:

* Expense metadata
* Expense ownership
* Split strategy

Attributes:

```cpp
class Expense
{
    int id;
    double amount;
    User* paidBy;
    vector<Split> splits;
};
```

---

## Split

Abstract representation of a split.

Responsibilities:

Defines share assigned to a participant.

---

## EqualSplit

Derived from Split.

Implements equal distribution.

---

## ExactSplit

Derived from Split.

Implements fixed amount allocation.

---

## PercentageSplit

Derived from Split.

Implements percentage-based allocation.

---

## ExpenseManager

Business layer responsible for:

* Creating expenses
* Validating splits
* Updating balances

---

## GroupManager

Responsible for:

* Group creation
* Member management

---

## DebtSimplifier

Responsible for:

* Balance computation
* Debt minimization

---

## DatabaseManager

Responsible for:

* SQL queries
* Persistence
* Transaction management

---

# OOP Principles Demonstrated

## Encapsulation

Data is hidden inside classes.

Example:

```cpp
class User
{
private:
    int id;
    string name;
};
```

Access occurs through public methods.

---

## Abstraction

Users interact with high-level interfaces.

Example:

```cpp
ExpenseManager.createExpense()
```

without knowing database internals.

---

## Inheritance

```cpp
Split
  |
  +---- EqualSplit
  |
  +---- ExactSplit
  |
  +---- PercentageSplit
```

All split types inherit common behavior.

---

## Polymorphism

Runtime selection of split strategy.

```cpp
Split* strategy;
```

Can point to:

```cpp
EqualSplit
ExactSplit
PercentageSplit
```

without changing caller logic.

---

# SOLID Principles Used

## Single Responsibility Principle

Each class performs one responsibility.

Examples:

* DatabaseManager → Database
* DebtSimplifier → Settlement
* ExpenseManager → Expenses

---

## Open Closed Principle

New split strategies can be added without modifying existing code.

Example:

```cpp
WeightedSplit
```

can be added later.

---

## Dependency Separation

Business logic is separated from persistence logic.

---

# Graph Theory Usage

Debt relationships are modeled as a directed weighted graph.

---

## Graph Representation

```text
A ----500----> B
B ----200----> C
C ----100----> D
```

Meaning:

Edge:

```text
u -> v
```

indicates:

```text
u owes v
```

weight amount.

---

# Data Structure Used

```cpp
unordered_map<int,
              unordered_map<int,double>>
```

Purpose:

Adjacency list representation.

---

# Balance Calculation

For each edge:

```text
debtor -> creditor
```

Net balance is computed.

Example:

```text
A owes B 500
B owes C 500
```

Net:

```text
A = -500
B = 0
C = +500
```

---

# Debt Simplification Algorithm

Goal:

Reduce number of transactions.

---

## Step 1

Compute net balance for every user.

```text
Negative = debtor
Positive = creditor
```

---

## Step 2

Create two collections.

```text
Debtors
Creditors
```

---

## Step 3

Match largest debtor with largest creditor.

Example:

```text
A = -1000
B = -500

C = +700
D = +800
```

Transactions:

```text
A -> D = 800
A -> C = 200
B -> C = 500
```

---

# Algorithm Complexity

Let:

```text
N = users
E = debts
```

Balance Calculation:

```text
O(E)
```

Settlement Generation:

```text
O(N log N)
```

(using priority queues)

---

# Data Structures Used

## Vector

```cpp
vector<User>
vector<Group>
vector<Expense>
```

Reason:

Dynamic storage.

---

## Hash Map

```cpp
unordered_map
```

Reason:

O(1) average lookup.

---

## Queue

Used in request handling.

---

## Priority Queue

Used in debt simplification.

Complexity:

```text
Insert : O(log N)
Remove : O(log N)
```

---

# REST API Design

The project follows REST principles.

---

## User APIs

```http
POST /users
GET /users/{id}
```

---

## Group APIs

```http
POST /groups
POST /groups/members
GET /groups/{id}
```

---

## Expense APIs

```http
POST /expenses
GET /expenses/{id}
```

---

## Balance APIs

```http
GET /groups/{id}/balances
```

Returns:

```json
{
  "user1": -500,
  "user2": 500
}
```

---

## Settlement APIs

```http
GET /groups/{id}/settlements
```

Returns minimized transactions.

---

# Design Patterns Used

## Strategy Pattern

Used for split calculations.

```text
SplitStrategy
      |
      +---- EqualSplit
      +---- ExactSplit
      +---- PercentageSplit
```

Benefits:

* Extensible
* Clean design
* Open for future split types

---

## Repository Pattern

Separates database operations from business logic.

Benefits:

* Maintainability
* Testability
* Loose coupling

---

# Error Handling

System validates:

* Invalid users
* Invalid groups
* Percentage sum ≠ 100
* Exact split mismatch
* Negative amounts
* Duplicate members

---

# Future Enhancements

* JWT Authentication
* OAuth Login
* Notifications
* Currency Conversion
* Recurring Expenses
* Redis Cache
* PostgreSQL Migration
* Docker Deployment
* Microservice Architecture
* Audit Logging

---

# Key Learnings

This project demonstrates practical understanding of:

* Backend Development
* REST APIs
* SQL Databases
* Object-Oriented Programming
* SOLID Principles
* Graph Algorithms
* Data Structures
* Software Architecture
* Transaction Management
* Real-world System Design

It simulates the core architecture of a production-grade expense sharing platform while remaining lightweight enough to be implemented completely in modern C++.
