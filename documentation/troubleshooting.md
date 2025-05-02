# QNIX Package Manager Troubleshooting Guide

## Common Issues

### Store Issues
1. **Store Path Permission Errors**
   - Verify store directory permissions (755)
   - Check file ownership
   - Run with appropriate privileges

2. **Package Hash Verification Failed**
   - Verify file integrity
   - Check for file corruption
   - Recompute hash with --verify

3. **Dependency Resolution Failures**
   - Ensure all required libraries exist
   - Check boot library mapping
   - Verify library paths

### Profile Issues
1. **Shell Environment Problems**
   - Verify LD_LIBRARY_PATH
   - Check wrapper scripts
   - Validate essential utilities

2. **Generation Management**
   - Check system time accuracy
   - Verify profile permissions
   - Clear old generations

3. **Library Link Errors**
   - Check symlink validity
   - Verify library existence
   - Update library mappings

### Database Issues
1. **Database Corruption**
   - Backup existing data
   - Reinitialize database
   - Recover from backup

2. **Reference Tracking**
   - Verify reference integrity
   - Update dependency links
   - Check root markers

## Error Messages

### Common Error Messages
- "Failed to compute store path": Hash computation issue
- "Cannot create symlink": Permission/path problem
- "Library not found": Missing dependency
- "Invalid generation": Time sync/backup issue
- "Database access failed": Permission/corruption

## Recovery Procedures

### Store Recovery
1. Verify store structure
2. Check file permissions
3. Rebuild database entries
4. Recompute package hashes

### Profile Recovery
1. Roll back to last good generation
2. Rebuild profile links
3. Verify library paths
4. Update wrapper scripts

### Database Recovery
1. Backup corrupt database
2. Initialize new database
3. Rebuild from store content
4. Verify reference integrity