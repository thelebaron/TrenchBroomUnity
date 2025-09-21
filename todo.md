# TrenchBroom Unity Fork TODO

## Purpose
This fork of TrenchBroom adds unique index tracking to each entity class for Unity use. A custom map importer imports Quake maps to Unity, where entity names must be unique. Previously using just the classname caused uniqueness violation warnings during subsequent imports, so this branch adds a property giving each class a tracked index to ensure unique naming in Unity.

## Current Issues

### Issue 1: Index Gap-Filling Not Working (Low Priority)
**Problem**: The gap-filling fix doesn't work because existing entities already have ClassIndex properties, so `ensureClassIndex()` returns early (Map.cpp:1361) without calling `allocateClassIndex()`.

**Example**: Create 5 entities (indices 1-5), delete 4 entities, create new entity → gets index 6 instead of reusing index 2.

**Potential Solutions**:
- Clear existing class indices when entities are deleted
- Modify `ensureClassIndex()` to validate and potentially reassign existing indices
- Reset the high water mark when significant gaps exist

### Issue 3: Excessive Debug Logging (Medium Priority)
**Problem**: Debug logging is enabled showing verbose transaction and model renderer logs:
```
Constructed entity model renderer for ModelSpecification{path: "progs\\soldier.mdl", skinIndex: 0, frameIndex: 0}
Starting transaction 'Select Object'
Command 'Select 1 Object' executed
Committing transaction
Transaction 'Select Object' executed
```

**Source Locations**:
- Transaction logs: Map.cpp:1690, 1704, 1942
- Entity model renderer logs: EntityModelManager.cpp:109

**Solutions**:
- Change `logger().debug()` to `logger().trace()` for verbose operations
- Add conditional debug logging based on build configuration
- Disable debug logging in release builds