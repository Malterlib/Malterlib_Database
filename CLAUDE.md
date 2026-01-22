# CLAUDE.md - Database Module

This file provides guidance to Claude Code (claude.ai/code) when working with the Database module.

## Module Overview

The Database module provides a high-performance embedded key-value database based on LMDB (Lightning Memory-Mapped Database). It offers ACID transactions, zero-copy reads, multi-thread and multi-process concurrency, and memory-mapped storage for extremely fast performance.

### Key Features
- **ACID Transactions** - Full transaction support with read and write transactions
- **Zero-Copy Operations** - Direct memory-mapped access for optimal performance
- **Multi-Version Concurrency Control (MVCC)** - Non-blocking reads with consistent snapshots
- **Nested Transactions** - Support for child transactions within write transactions
- **Multiple Named Databases** - Support for multiple named sub-databases within an environment
- **Sorted Duplicate Data** - Support for multiple values per key with sorted storage
- **Async Operations** - DatabaseActor provides async/await interface for database operations
- **Serialization Framework** - DatabaseValue provides type-safe serialization/deserialization
- **Query Results** - ResultSet provides structured query result representation

## Architecture

### Core Components

#### CDatabaseEnvironment
The main database environment that manages the memory-mapped file and transactions.

```cpp
// Create and open a database environment
CDatabaseEnvironment Environment;
Environment.f_SetMaxDatabases(32);
Environment.f_SetMapSize(1024 * 1024 * 1024); // 1GB
Environment.f_Open("/path/to/database", EDatabaseEnvironmentFlag_None, EFileAttrib_None);

// Begin transactions
CDatabaseReadTransaction ReadTransaction = Environment.f_BeginReadTransaction();
CDatabaseWriteTransaction WriteTransaction = Environment.f_BeginWriteTransaction();
```

#### CDatabaseSub
Named sub-databases within an environment. Each sub-database is a separate B+tree.

```cpp
// Open or create a named database
CDatabaseSub Database = WriteTransaction.f_OpenDatabase("UserData", EDatabaseSubFlag_Create);

// Open the main (unnamed) database
CDatabaseSub MainDatabase = WriteTransaction.f_OpenDatabase();
```

#### CDatabaseValue
Type-safe serialization system for keys and values.

```cpp
// Serialize data to database values
CDatabaseValueCache KeyCache
CDatabaseValueCache ValueCache;

// Simple types
CDatabaseValue Key = CDatabaseValue::fs_ToLexicographic(KeyCache, "User", 12345);
CDatabaseValue Value = CDatabaseValue::fs_ToStream(ValueCache, "John Doe", 30, true);

// Tuple serialization
auto KeyTuple = NStorage::fg_TupleReferences("Category", "Products", 42);
CDatabaseValue Key2 = CDatabaseValue::fs_ToLexicographic(KeyCache, KeyTuple);

// Deserialization
auto [Category, Name, ID] = Value.f_TupleFromLexicographic<CStr, CStr, uint32>();
```

#### Transactions and Cursors
ACID transactions with cursor support for iteration.

```cpp
// Write transaction with cursor
{
	CDatabaseWriteTransaction WriteTransaction = Environment.f_BeginWriteTransaction();
	CDatabaseSub Database = WriteTransaction.f_OpenDatabase("Products");
	CDatabaseSubWriteTransaction DatabaseTransaction = Database.f_Transaction(WriteTransaction);

	// Insert data
	DatabaseTransaction.f_Insert("Key1", "Value1");
	DatabaseTransaction.f_Insert
		(
			NStorage::fg_TupleReferences("Product", 100)
			, NStorage::fg_TupleReferences("Laptop", 999.99, true)
		)
	;

	// Iterate with cursor
	for (CDatabaseCursorWrite iCursor = DatabaseTransaction.f_WriteCursor(); iCursor.f_First(); iCursor;)
	{
		CDatabaseValue const &Key = iCursor.f_Key();
		CDatabaseValue const &Value = iCursor.f_Value();

		// Decode key and value
		auto [Category, ID] = Key.f_TupleFromLexicographic<CStr, uint32>();
		auto [Name, Price, InStock] = Value.f_TupleFromStream<CStr, fp64, bool>();

		// Modify or delete
		if (!InStock)
			iCursor.f_Delete();
		else
			++iCursor;
	}

	WriteTransaction.f_Commit();
}

// Read transaction
{
	CDatabaseReadTransaction ReadTransaction = Environment.f_BeginReadTransaction();
	CDatabaseSubReadTransaction DatabaseTransaction = ReadTransaction.f_OpenDatabaseTransaction("Products");

	// Find specific key
	CDatabaseValueCache KeyCache;
	CDatabaseValue SearchKey = CDatabaseValue::fs_ToLexicographic(KeyCache, "Product", 100);

	if (CDatabaseValue Value = DatabaseTransaction.f_Get(SearchKey))
	{
		auto [Name, Price, InStock] = Value.f_TupleFromStream<CStr, fp64, bool>();
		// Use the data
	}
}
```

#### CDatabaseActor
Async/await interface for database operations using the actor model.

```cpp
// Database actor for async operations
TCActor<CDatabaseActor> DatabaseActor = fg_Construct();

// Open database asynchronously
co_await DatabaseActor(&CDatabaseActor::f_OpenDatabase, "/path/to/database", 1024 * 1024 * 1024);

// Async read transaction
{
	CDatabaseActor::CTransactionRead ReadTransaction = co_await DatabaseActor(&CDatabaseActor::f_OpenTransactionRead);

	// Read operations... ReadTransaction.m_Transaction
}

// Async write transaction
{
	CDatabaseActor::CTransactionWrite WriteTransaction = co_await DatabaseActor(&CDatabaseActor::f_OpenTransactionWrite);

	// Write operations... with WriteTransaction.m_Transaction

	co_await DatabaseActor(&CDatabaseActor::f_CommitWriteTransaction, fg_Move(WriteTransaction));
}

// Compaction with async operation
co_await DatabaseActor
	(
		&CDatabaseActor::f_WriteWithCompaction
		, g_ActorFunctorWeak / [](CDatabaseActor::CTransactionWrite _Transaction, bool _bCompacting) -> NConcurrency::TCFuture<CDatabaseActor::CTransactionWrite>
		{
			// Perform write operations
			// If _bCompacting is true, this is running during compaction
			co_return fg_Move(_Transaction);
		}
	)
;
```

### File Organization

```
Database/
├── Include/Mib/Database/       # Public headers
│   ├── Database                # Main database interface
│   ├── DatabaseActor           # Async actor interface
│   ├── DatabaseValue           # Serialization system
│   ├── ResultSet               # Query result representation
│   └── ResultSetBuilder        # Result set construction
├── Source/
│   ├── Database/               # Core database implementation (LMDB-based)
│   │   ├── *_Database.cpp      # Environment and transaction management
│   │   ├── *_Cursor.*          # Cursor implementation
│   │   ├── *_Sub.*             # Sub-database implementation
│   │   └── *_Transaction.*     # Transaction implementation
│   ├── DatabaseActor/          # Async actor implementation
│   ├── DatabaseValue/          # Serialization implementation
│   │   ├── *_Integer.hpp       # Integer serialization
│   │   ├── *_String.hpp        # String serialization
│   │   ├── *_Float.hpp         # Float serialization
│   │   ├── *_Tuple.hpp         # Tuple serialization
│   │   ├── *_Vector.hpp        # Vector serialization
│   │   └── *_Map.hpp           # Map serialization
│   └── ResultSet/              # Result set implementation
└── Test/                       # Unit tests
```

## Usage Patterns

### Database Compaction

```cpp
TCFuture<void> fg_Compaction(CDatabaseEnvironment &_Environment)
{
	// Copy database with compaction
	_Environment.f_Copy("/path/to/compacted.db", EDatabaseEnvironmentCopyFlag_Compact);

	// Or use DatabaseActor for online compaction
	TCActor<CDatabaseActor> Actor = fg_Construct();
	co_await Actor(&CDatabaseActor::f_OpenDatabase, "/path/to/database", 1024 * 1024 * 1024);

	CDatabaseActor::CTransactionWrite Transaction = co_await Actor(&CDatabaseActor::f_OpenTransactionWrite);
	co_await Actor(&CDatabaseActor::f_Compact, fg_Move(Transaction), 512 * 1024 * 1024); // New size
}
```

## Environment Flags

```cpp
// Common environment flags
EDatabaseEnvironmentFlag_None                    // Default settings
EDatabaseEnvironmentFlag_NoSubdir                // Database file directly, not directory
EDatabaseEnvironmentFlag_ReadOnly                // Open in read-only mode
EDatabaseEnvironmentFlag_NoSync                  // Don't sync to disk (faster but less safe)
EDatabaseEnvironmentFlag_NoMetaSync              // Don't sync metadata (faster commits)
EDatabaseEnvironmentFlag_WriteMap                // Use writable memory map
EDatabaseEnvironmentFlag_NoLock                  // No locking (single process only)
EDatabaseEnvironmentFlag_NoReadAhead             // Disable read-ahead
EDatabaseEnvironmentFlag_FixedMap                // Use fixed address for mmap (experimental)
```

## Sub-Database Flags

```cpp
// Common sub-database flags
EDatabaseSubFlag_None                            // Default settings
EDatabaseSubFlag_Create                          // Create if doesn't exist
EDatabaseSubFlag_ReverseKey                      // Reverse key comparison
EDatabaseSubFlag_SortedDuplicates                // Allow sorted duplicate data
EDatabaseSubFlag_IntegerKey                      // Keys are integers
EDatabaseSubFlag_SortedDuplicatesFixed           // Fixed-size duplicate data
EDatabaseSubFlag_IntegerDuplicates               // Duplicate data are integers
EDatabaseSubFlag_ReverseDuplicates               // Reverse duplicate data comparison
```

## Testing

The module includes comprehensive tests in `Test/Test_Malterlib_Database_*.cpp`:

```bash
# Build and run database tests
MalterlibBuildShowProgress=false ./mib build Tests
/opt/Deploy/Tests/RunAllTests --paths '["Malterlib/Database/*"]'

# Run specific database tests
/opt/Deploy/Tests/RunAllTests --paths '["Malterlib/Database/Database/Basics"]'
/opt/Deploy/Tests/RunAllTests --paths '["Malterlib/Database/DatabaseValue/*"]'
/opt/Deploy/Tests/RunAllTests --paths '["Malterlib/Database/ResultSet/*"]'
```

## Important Notes

### Performance Considerations
- Database environment map size must be set before opening (cannot grow dynamically)
- Use lexicographic encoding for keys to maintain sort order
- Batch operations in single transaction for better performance
- Read transactions are extremely cheap - use liberally
- Write transactions serialize - minimize duration

### Memory Management
- Database values returned from queries point directly into memory-mapped file
- Values are only valid during transaction lifetime
- Copy data if needed beyond transaction scope
- DatabaseValueCache manages temporary serialization buffers

### Concurrency
- Multiple readers, single writer model (MVCC)
- Readers never block writers, writers never block readers
- Read transactions see consistent snapshot
- Use DatabaseActor for async operations in concurrent code

### Data Encoding
- **Lexicographic encoding** (`fs_ToLexicographic`) - Preserves sort order, use for keys
- **Stream encoding** (`fs_ToStream`) - Use for values
- Descending wrapper available for reverse sort order
- All standard types supported: integers, floats, strings, containers

### Transaction Best Practices
- Always commit or abort transactions explicitly
- Keep write transactions as short as possible
- Use child transactions for complex multi-step operations, but be aware of limitations in LMDB
- Check exceptions - operations can fail (e.g., disk full)

### Platform Support
- Cross-platform: Windows, macOS, Linux
- 32-bit and 64-bit architectures
- Endian-neutral storage format
- File locking for multi-process access (except with NoLock flag)

## Code Standards

All code in this module follows Malterlib coding standards:

### Naming
- Types: `CDatabaseEnvironment`, `CDatabaseValue` (C prefix for classes)
- Functions: `f_Open()`, `fp_Close()` (f_ for public, fp_ for private)
- Members: `m_Size`, `mp_pData` (m_ for member, mp_ for private member)
- Static: `fs_ToLexicographic()` (fs_ for static function)
- Enums: `EDatabaseEnvironmentFlag_ReadOnly` (E prefix, underscore separator)

### Structure
- Header files in `Include/Mib/Database/`
- Implementation in `Source/[Component]/`
- Tests in `Test/`
- One tab indentation (4 columns)
- Braces on new line
