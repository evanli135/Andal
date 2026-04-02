  Core sections:                                                                                                                                                                        
  - Architecture overview (columnar, append-only, indexed)                                                                                                                              
  - Complete project structure (where every file goes)                                                                                                                                  
  - Python API design (what users will write)         
  - C internals (data structures, core functions)                                                                                                                                       
  - 5 implementation phases (MVP → compression)                                                                                                                                         
  - File format specification                                                                                                                                                           
  - Performance targets                                                                                                                                                                 
                                                                                                                                                                                        
  Key decisions made:                                                                                                                                                                   
  - Columnar storage with time-based partitions                                                                                                                                         
  - Inverted indexes for event_type and user_id                                                                                                                                         
  - Bitmap filtering for fast queries          
  - mmap for persistence                                                                                                                                                                
  - Dictionary encoding for strings                                                                                                                                                     
                                                                                                                                                                                        
  Phase 1 focus (next 2-3 days):                                                                                                                                                        
  - In-memory columnar arrays                                                                                                                                                           
  - Basic append + linear scan filter                                                                                                                                                   
  - Minimal Python bindings                                                                                                                                                             
  - Tests                                                                                                                                                                               
                                