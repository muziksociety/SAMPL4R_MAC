#!/usr/bin/env powershell
# SAMPLERCLI - SAMPLAR Command Line Interface (Windows)
# Cross-platform backend testing wrapper
# Usage: ./scripts/sm5cli.ps1 <command> [options]

param(
    [Parameter(Mandatory=$true, Position=0)]
    [ValidateSet("test", "build", "pipeline", "status", "help", "batch")]
    [string]$Command,
    
    [string]$Category = "",
    [string]$Input = "",
    [string]$OutputDir = "tests/results",
    [switch]$VerboseOutput,
    [switch]$Universal,
    [switch]$Separation,
    [int]$Variations = 5,
    [string]$Engine = "all",
    [string]$Output = "json",
    [string]$Platform = "auto",
    [string]$Type = "debug",
    [switch]$Verify,
    [string]$LogLevel = "info",
    
    # Batch-specific parameters
    [string]$BatchAction = "",
    [string]$BatchName = "",
    [string]$BatchId = "",
    [string]$Description = "",
    [string]$InputDir = "",
    [string]$Pattern = "*.wav;*.mp3;*.flac;*.aiff",
    [int]$Concurrent = 2,
    [string]$Priority = "normal",
    [switch]$Watch,
    [string]$Engines = "FastSimpleVariation"
)

# Configuration
$SAMPLAR_ROOT = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BUILD_DIR = "$SAMPLAR_ROOT\build"
$SM5TESTS_EXE = "$BUILD_DIR\Debug\SM5Tests.exe"
$RESULTS_DIR = "$SAMPLAR_ROOT\$OutputDir"
$BATCHES_DIR = "$SAMPLAR_ROOT\batches"
$BATCH_QUEUE_FILE = "$BATCHES_DIR\batch_queue.json"
$TIMESTAMP = Get-Date -Format "yyyy-MM-ddTHH-mm-ss"

# Ensure directories exist
if (!(Test-Path $RESULTS_DIR)) {
    New-Item -ItemType Directory -Path $RESULTS_DIR -Force | Out-Null
}
if (!(Test-Path $BATCHES_DIR)) {
    New-Item -ItemType Directory -Path $BATCHES_DIR -Force | Out-Null
}

function Write-SM5Log {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    if ($VerboseOutput -or $LogLevel -eq "verbose") {
        Write-Host "[$timestamp] [$Level] $Message" -ForegroundColor Cyan
    }
}

# ============ BATCH MANAGEMENT FUNCTIONS ============

function Get-BatchQueue {
    if (!(Test-Path $BATCH_QUEUE_FILE)) {
        # Initialize empty queue if it doesn't exist
        $emptyQueue = @{
            schema_version = "1.0"
            created = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
            batches = @{}
            settings = @{
                default_concurrent_separation = 2
                default_concurrent_processing = 3
                default_confidence_threshold = 0.85
                default_engines = @("FastSimpleVariation", "CreativeVariationEngine")
                default_variations = 5
                cleanup_completed_after_days = 7
            }
        }
        $emptyQueue | ConvertTo-Json -Depth 10 | Out-File $BATCH_QUEUE_FILE -Encoding UTF8
    }
    
    $queueJson = Get-Content $BATCH_QUEUE_FILE -Raw | ConvertFrom-Json
    return $queueJson
}

function Save-BatchQueue {
    param($Queue)
    $Queue | ConvertTo-Json -Depth 10 | Out-File $BATCH_QUEUE_FILE -Encoding UTF8
}

function New-BatchJob {
    param([string]$Name, [string]$Description = "")
    
    Write-SM5Log "Creating new batch job: $Name"
    
    $queue = Get-BatchQueue
    $batchId = "batch_" + (Get-Date -Format "yyyyMMdd_HHmmss")
    
    $newBatch = @{
        id = $batchId
        name = $Name
        description = $Description
        created = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
        status = "active"
        files = @()
        settings = @{
            max_concurrent_separation = $queue.settings.default_concurrent_separation
            max_concurrent_processing = $queue.settings.default_concurrent_processing
            confidence_threshold = $queue.settings.default_confidence_threshold
            engines = $queue.settings.default_engines
            variations = $queue.settings.default_variations
        }
        stats = @{
            total_files = 0
            pending = 0
            separating = 0
            separated = 0
            reviewing = 0
            ready = 0
            processing = 0
            complete = 0
            failed = 0
        }
    }
    
    # Add batch to queue using Add-Member to avoid property issues
    $queue.batches | Add-Member -Type NoteProperty -Name $batchId -Value $newBatch
    Save-BatchQueue $queue
    
    # Create batch directory
    $batchDir = "$BATCHES_DIR\$batchId"
    New-Item -ItemType Directory -Path $batchDir -Force | Out-Null
    New-Item -ItemType Directory -Path "$batchDir\separated" -Force | Out-Null
    New-Item -ItemType Directory -Path "$batchDir\processed" -Force | Out-Null
    New-Item -ItemType Directory -Path "$batchDir\logs" -Force | Out-Null
    
    Write-SM5Log "Batch job created: $batchId"
    
    $result = @{
        platform = "windows"
        command = "batch create"
        status = "success"
        batch_id = $batchId
        batch_name = $Name
        batch_dir = $batchDir
        timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    }
    
    return $result
}

function Add-FilesToBatch {
    param([string]$BatchId, [string]$InputPath, [string]$Priority = "normal", [string]$Pattern = "*.wav;*.mp3;*.flac;*.aiff")
    
    Write-SM5Log "Adding files to batch: $BatchId from path: $InputPath"
    
    $queue = Get-BatchQueue
    $batchProperty = $queue.batches.PSObject.Properties | Where-Object { $_.Name -eq $BatchId } | Select-Object -First 1
    if (-not $batchProperty) {
        Write-Error "Batch not found: $BatchId"
        return $false
    }
    $batchData = $batchProperty.Value
    
    $files = @()
    
    Write-SM5Log "Testing path: '$InputPath' with Pattern: '$Pattern'"
    if (Test-Path $InputPath -PathType Container) {
        # Directory input - scan for matching files
        Write-SM5Log "Scanning directory: $InputPath"
        $patterns = $Pattern -split ";" | ForEach-Object { $_.Trim() }
        Write-SM5Log "Patterns to scan: $($patterns -join ', ')"
        foreach ($pattern in $patterns) {
            if ($pattern) {
                Write-SM5Log "Scanning for pattern: $pattern"
                $matchingFiles = Get-ChildItem -Path $InputPath -Filter $pattern -File
                Write-SM5Log "Found $($matchingFiles.Count) files for pattern $pattern"
                $files += $matchingFiles
            }
        }
    }
    elseif (Test-Path $InputPath -PathType Leaf) {
        # Single file input
        $files += Get-Item $InputPath
    }
    else {
        Write-Error "Input path not found: $InputPath"
        return $false
    }
    
    $addedCount = 0
    foreach ($file in $files) {
        $fileId = "file_" + (Get-Date -Format "yyyyMMdd_HHmmss_fff")
        
        $batchFile = @{
            id = $fileId
            input_path = $file.FullName
            filename = $file.Name
            size_bytes = $file.Length
            status = "pending"
            stage = "queued"
            priority = $Priority
            created = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
            separation = @{
                status = "not_started"
                drums_path = $null
                confidence = $null
                duration_ms = $null
                started = $null
                completed = $null
                error = $null
            }
            review = @{
                status = "not_started"
                bpm = $null
                grid_aligned = $false
                selection_start = $null
                selection_end = $null
                processing_preset = "default"
                reviewed_by = $null
                reviewed_at = $null
            }
            processing = @{
                status = "not_started"
                engines = $batchData.settings.engines
                variations_count = $batchData.settings.variations
                output_dir = $null
                duration_ms = $null
                started = $null
                completed = $null
                error = $null
            }
        }
        
        $batchData.files = $batchData.files + $batchFile
        $addedCount++
    }
    
    # Update stats
    $batchData.stats.total_files += $addedCount
    $batchData.stats.pending += $addedCount
    
    Save-BatchQueue $queue
    
    Write-SM5Log "Added $addedCount files to batch $BatchId"
    
    $result = @{
        platform = "windows"
        command = "batch add"
        status = "success"
        batch_id = $BatchId
        files_added = $addedCount
        total_files = $batchData.stats.total_files
        timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    }
    
    return $result
}

function Get-BatchList {
    $queue = Get-BatchQueue
    $batches = @()
    
    foreach ($batchId in $queue.batches.PSObject.Properties.Name) {
        $batch = $queue.batches.$batchId
        $batches += @{
            id = $batch.id
            name = $batch.name
            description = $batch.description
            created = $batch.created
            status = $batch.status
            total_files = $batch.stats.total_files
            pending = $batch.stats.pending
            complete = $batch.stats.complete
            failed = $batch.stats.failed
        }
    }
    
    return $batches
}

function Get-BatchStatus {
    param([string]$BatchId, [switch]$Watch)
    
    do {
        $queue = Get-BatchQueue
        if (-not $queue.batches.$BatchId) {
            Write-Error "Batch not found: $BatchId"
            return $false
        }
        
        $batch = $queue.batches.$BatchId
        
        if ($Output -eq "json") {
            $status = @{
                platform = "windows"
                command = "batch status"
                batch_id = $BatchId
                batch_name = $batch.name
                status = $batch.status
                stats = $batch.stats
                files = $batch.files
                timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
            }
            $status | ConvertTo-Json -Depth 10
        }
        else {
            Clear-Host
            Write-Host "Batch Status: $($batch.name) ($BatchId)" -ForegroundColor Green
            Write-Host "Created: $($batch.created)"
            Write-Host "Status: $($batch.status)"
            Write-Host ""
            Write-Host "Progress Overview:" -ForegroundColor Cyan
            Write-Host "  Total Files: $($batch.stats.total_files)"
            Write-Host "  Pending: $($batch.stats.pending)" -ForegroundColor Yellow
            Write-Host "  Separating: $($batch.stats.separating)" -ForegroundColor Blue
            Write-Host "  Separated: $($batch.stats.separated)" -ForegroundColor Magenta
            Write-Host "  Ready: $($batch.stats.ready)" -ForegroundColor Cyan
            Write-Host "  Processing: $($batch.stats.processing)" -ForegroundColor Blue
            Write-Host "  Complete: $($batch.stats.complete)" -ForegroundColor Green
            Write-Host "  Failed: $($batch.stats.failed)" -ForegroundColor Red
            Write-Host ""
            
            if ($batch.files.Count -gt 0) {
                Write-Host "Files:" -ForegroundColor White
                foreach ($file in $batch.files) {
                    $statusColor = switch ($file.status) {
                        "pending" { "Yellow" }
                        "separating" { "Blue" }
                        "separated" { "Magenta" }
                        "ready" { "Cyan" }
                        "processing" { "Blue" }
                        "complete" { "Green" }
                        "failed" { "Red" }
                        default { "White" }
                    }
                    Write-Host "  [$($file.status.PadRight(10))] $($file.filename)" -ForegroundColor $statusColor
                }
            }
        }
        
        if ($Watch) {
            Write-Host ""
            Write-Host "Refreshing in 3 seconds... (Ctrl+C to stop)" -ForegroundColor Gray
            Start-Sleep -Seconds 3
        }
    } while ($Watch)
    
    return $true
}

function Start-BatchSeparation {
    param([string]$BatchId, [int]$MaxConcurrent = 2)
    
    Write-SM5Log "Starting batch separation: $BatchId with max concurrent: $MaxConcurrent"
    
    $queue = Get-BatchQueue
    $batchProperty = $queue.batches.PSObject.Properties | Where-Object { $_.Name -eq $BatchId } | Select-Object -First 1
    if (-not $batchProperty) {
        Write-Error "Batch not found: $BatchId"
        return $false
    }
    $batchData = $batchProperty.Value
    
    # Get pending files
    $pendingFiles = $batchData.files | Where-Object { $_.status -eq "pending" }
    if ($pendingFiles.Count -eq 0) {
        Write-SM5Log "No pending files to separate in batch $BatchId"
        return $true
    }
    
    Write-SM5Log "Found $($pendingFiles.Count) pending files to separate"
    
    # Process files with concurrency limit
    $activeJobs = @()
    $processedCount = 0
    
    foreach ($file in $pendingFiles) {
        # Wait if we've hit concurrent limit
        while ($activeJobs.Count -ge $MaxConcurrent) {
            Write-SM5Log "Waiting for active jobs to complete... ($($activeJobs.Count)/$MaxConcurrent active)"
            Start-Sleep -Seconds 2
            
            # Check for completed jobs
            $completedJobs = @()
            foreach ($jobTracker in $activeJobs) {
                if ($jobTracker.Process.HasExited) {
                    $completedJobs += $jobTracker
                }
            }
            
            # Process completed jobs
            foreach ($completedJob in $completedJobs) {
                $processedCount++
                $exitCode = if ($completedJob.Process.HasExited) { $completedJob.Process.ExitCode } else { -1 }
                Write-SM5Log "Job completed: $($completedJob.FileName) (Exit code: $exitCode)"
                $activeJobs = $activeJobs | Where-Object { $_ -ne $completedJob }
                
                # Update file status based on job result
                for ($i = 0; $i -lt $batchData.files.Count; $i++) {
                    if ($batchData.files[$i].id -eq $completedJob.FileId) {
                        if ($exitCode -eq 0) {
                            $batchData.files[$i].status = "separated"
                            $batchData.files[$i].separation.status = "completed" 
                            $batchData.files[$i].separation.completed = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                            $batchData.files[$i].separation.drums_path = "$BATCHES_DIR\$BatchId\separated\$($batchData.files[$i].filename)_drums.wav"
                        } else {
                            $batchData.files[$i].status = "failed"
                            $batchData.files[$i].separation.status = "failed"
                            $batchData.files[$i].separation.error = "Separation process failed with exit code $exitCode"
                        }
                        break
                    }
                }
            }
            
            if ($completedJobs.Count -gt 0) {
                # Update stats and save queue
                Update-BatchStats $queue $BatchId
                Save-BatchQueue $queue
            }
        }
        
        # Start new separation job
        Write-SM5Log "Starting separation for: $($file.filename)"
        
        # Update file status to separating
        for ($i = 0; $i -lt $batchData.files.Count; $i++) {
            if ($batchData.files[$i].id -eq $file.id) {
                $batchData.files[$i].status = "separating"
                $batchData.files[$i].separation.status = "in_progress"
                $batchData.files[$i].separation.started = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                break
            }
        }
        
        # Simulate separation process (replace with actual FullSongService call)
        $separationProcess = Start-Process -FilePath "powershell.exe" -ArgumentList @(
            "-ExecutionPolicy", "Bypass",
            "-Command", "Start-Sleep -Seconds 3; Write-Host 'Separating $($file.filename)...'; exit 0"
        ) -NoNewWindow -PassThru
        
        # Create job tracking object
        $jobTracker = @{
            Process = $separationProcess
            FileId = $file.id
            FileName = $file.filename
        }
        
        $activeJobs += $jobTracker
        Write-SM5Log "Started separation job for $($file.filename) (PID: $($separationProcess.Id))"
    }
    
    # Wait for all remaining jobs to complete
    while ($activeJobs.Count -gt 0) {
        Write-SM5Log "Waiting for final $($activeJobs.Count) jobs to complete..."
        Start-Sleep -Seconds 2
        
        $completedJobs = @()
        foreach ($jobTracker in $activeJobs) {
            if ($jobTracker.Process.HasExited) {
                $completedJobs += $jobTracker
            }
        }
        
        foreach ($completedJob in $completedJobs) {
            $processedCount++
            $finalExitCode = if ($completedJob.Process.HasExited) { $completedJob.Process.ExitCode } else { -1 }
            Write-SM5Log "Final job completed: $($completedJob.FileName) (Exit code: $finalExitCode)"
            $activeJobs = $activeJobs | Where-Object { $_ -ne $completedJob }
            
            # Update file status
            for ($i = 0; $i -lt $batchData.files.Count; $i++) {
                if ($batchData.files[$i].id -eq $completedJob.FileId) {
                    if ($finalExitCode -eq 0) {
                        $batchData.files[$i].status = "separated"
                        $batchData.files[$i].separation.status = "completed"
                        $batchData.files[$i].separation.completed = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                        $batchData.files[$i].separation.drums_path = "$BATCHES_DIR\$BatchId\separated\$($batchData.files[$i].filename)_drums.wav"
                    } else {
                        $batchData.files[$i].status = "failed"
                        $batchData.files[$i].separation.status = "failed"
                        $batchData.files[$i].separation.error = "Separation process failed with exit code $finalExitCode"
                    }
                    break
                }
            }
        }
        
        if ($completedJobs.Count -gt 0) {
            Update-BatchStats $queue $BatchId
            Save-BatchQueue $queue
        }
    }
    
    Write-SM5Log "Batch separation completed. Processed $processedCount files."
    
    $result = @{
        platform = "windows"
        command = "batch separate"
        batch_id = $BatchId
        files_processed = $processedCount
        status = "completed"
        timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    }
    
    return $result
}

function Update-BatchStats {
    param($Queue, $BatchId)
    
    $batchProperty = $Queue.batches.PSObject.Properties | Where-Object { $_.Name -eq $BatchId } | Select-Object -First 1
    if (-not $batchProperty) { return }
    
    $batch = $batchProperty.Value
    $stats = @{
        total_files = $batch.files.Count
        pending = 0
        separating = 0
        separated = 0
        reviewing = 0
        ready = 0
        processing = 0
        complete = 0
        failed = 0
    }
    
    foreach ($file in $batch.files) {
        switch ($file.status) {
            "pending" { $stats.pending++ }
            "separating" { $stats.separating++ }
            "separated" { $stats.separated++ }
            "reviewing" { $stats.reviewing++ }
            "ready" { $stats.ready++ }
            "processing" { $stats.processing++ }
            "complete" { $stats.complete++ }
            "failed" { $stats.failed++ }
        }
    }
    
    $batch.stats = $stats
}

function Start-BatchProcessing {
    param([string]$BatchId, [int]$MaxConcurrent = 3, [string]$Engines = "FastSimpleVariation")
    
    Write-SM5Log "Starting batch processing: $BatchId with engines: $Engines"
    
    $queue = Get-BatchQueue
    $batchProperty = $queue.batches.PSObject.Properties | Where-Object { $_.Name -eq $BatchId } | Select-Object -First 1
    if (-not $batchProperty) {
        Write-Error "Batch not found: $BatchId"
        return $false
    }
    $batchData = $batchProperty.Value
    
    # Get files ready for processing (separated and reviewed)
    $readyFiles = $batchData.files | Where-Object { $_.status -eq "separated" -or $_.status -eq "ready" }
    if ($readyFiles.Count -eq 0) {
        Write-SM5Log "No files ready for processing in batch $BatchId (need separated or ready status)"
        return $true
    }
    
    Write-SM5Log "Found $($readyFiles.Count) files ready for processing"
    
    # Parse engines
    $engineList = $Engines -split "," | ForEach-Object { $_.Trim() }
    Write-SM5Log "Processing with engines: $($engineList -join ', ')"
    
    # Process files with concurrency limit  
    $activeJobs = @()
    $processedCount = 0
    
    foreach ($file in $readyFiles) {
        # Wait if we've hit concurrent limit
        while ($activeJobs.Count -ge $MaxConcurrent) {
            Write-SM5Log "Waiting for active processing jobs... ($($activeJobs.Count)/$MaxConcurrent active)"
            Start-Sleep -Seconds 2
            
            # Check for completed jobs
            $completedJobs = @()
            foreach ($jobTracker in $activeJobs) {
                if ($jobTracker.Process.HasExited) {
                    $completedJobs += $jobTracker
                }
            }
            
            # Process completed jobs
            foreach ($completedJob in $completedJobs) {
                $processedCount++
                $exitCode = if ($completedJob.Process.HasExited) { $completedJob.Process.ExitCode } else { -1 }
                Write-SM5Log "Processing job completed: $($completedJob.FileName) (Exit code: $exitCode)"
                $activeJobs = $activeJobs | Where-Object { $_ -ne $completedJob }
                
                # Update file status based on job result
                for ($i = 0; $i -lt $batchData.files.Count; $i++) {
                    if ($batchData.files[$i].id -eq $completedJob.FileId) {
                        if ($exitCode -eq 0) {
                            $batchData.files[$i].status = "complete"
                            $batchData.files[$i].processing.status = "completed" 
                            $batchData.files[$i].processing.completed = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                            $batchData.files[$i].processing.output_dir = "$BATCHES_DIR\$BatchId\processed\$($batchData.files[$i].filename)_variations"
                        } else {
                            $batchData.files[$i].status = "failed"
                            $batchData.files[$i].processing.status = "failed"
                            $batchData.files[$i].processing.error = "Processing failed with exit code $exitCode"
                        }
                        break
                    }
                }
            }
            
            if ($completedJobs.Count -gt 0) {
                # Update stats and save queue
                Update-BatchStats $queue $BatchId
                Save-BatchQueue $queue
            }
        }
        
        # Start new processing job
        Write-SM5Log "Starting processing for: $($file.filename) with engines: $($engineList -join ',')"
        
        # Update file status to processing
        for ($i = 0; $i -lt $batchData.files.Count; $i++) {
            if ($batchData.files[$i].id -eq $file.id) {
                $batchData.files[$i].status = "processing"
                $batchData.files[$i].processing.status = "in_progress"
                $batchData.files[$i].processing.started = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                $batchData.files[$i].processing.engines = $engineList
                break
            }
        }
        
        # Create output directory for this file
        $outputDir = "$BATCHES_DIR\$BatchId\processed\$($file.filename)_variations"
        if (!(Test-Path $outputDir)) {
            New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
        }
        
        # Simulate processing with variation generation
        $variationsCount = $batchData.settings.variations
        $processingProcess = Start-Process -FilePath "powershell.exe" -ArgumentList @(
            "-ExecutionPolicy", "Bypass",
            "-Command", "Write-Host 'Processing $($file.filename) with engines: $($engineList -join ',')'; Start-Sleep -Seconds 4; Write-Host 'Generated $variationsCount variations'; exit 0"
        ) -NoNewWindow -PassThru
        
        # Create job tracking object
        $jobTracker = @{
            Process = $processingProcess
            FileId = $file.id
            FileName = $file.filename
        }
        
        $activeJobs += $jobTracker
        Write-SM5Log "Started processing job for $($file.filename) (PID: $($processingProcess.Id))"
    }
    
    # Wait for all remaining jobs to complete
    while ($activeJobs.Count -gt 0) {
        Write-SM5Log "Waiting for final $($activeJobs.Count) processing jobs to complete..."
        Start-Sleep -Seconds 2
        
        $completedJobs = @()
        foreach ($jobTracker in $activeJobs) {
            if ($jobTracker.Process.HasExited) {
                $completedJobs += $jobTracker
            }
        }
        
        foreach ($completedJob in $completedJobs) {
            $processedCount++
            $finalExitCode = if ($completedJob.Process.HasExited) { $completedJob.Process.ExitCode } else { -1 }
            Write-SM5Log "Final processing job completed: $($completedJob.FileName) (Exit code: $finalExitCode)"
            $activeJobs = $activeJobs | Where-Object { $_ -ne $completedJob }
            
            # Update file status
            for ($i = 0; $i -lt $batchData.files.Count; $i++) {
                if ($batchData.files[$i].id -eq $completedJob.FileId) {
                    if ($finalExitCode -eq 0) {
                        $batchData.files[$i].status = "complete"
                        $batchData.files[$i].processing.status = "completed"
                        $batchData.files[$i].processing.completed = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                        $batchData.files[$i].processing.output_dir = "$BATCHES_DIR\$BatchId\processed\$($batchData.files[$i].filename)_variations"
                    } else {
                        $batchData.files[$i].status = "failed"
                        $batchData.files[$i].processing.status = "failed"
                        $batchData.files[$i].processing.error = "Processing failed with exit code $finalExitCode"
                    }
                    break
                }
            }
        }
        
        if ($completedJobs.Count -gt 0) {
            Update-BatchStats $queue $BatchId
            Save-BatchQueue $queue
        }
    }
    
    Write-SM5Log "Batch processing completed. Processed $processedCount files."
    
    $result = @{
        platform = "windows"
        command = "batch process"
        batch_id = $BatchId
        files_processed = $processedCount
        engines_used = $engineList
        status = "completed"
        timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    }
    
    return $result
}

function Start-BatchReview {
    param([string]$BatchId)
    
    Write-SM5Log "Starting batch review: $BatchId"
    
    $queue = Get-BatchQueue
    $batchProperty = $queue.batches.PSObject.Properties | Where-Object { $_.Name -eq $BatchId } | Select-Object -First 1
    if (-not $batchProperty) {
        Write-Error "Batch not found: $BatchId"
        return $false
    }
    $batchData = $batchProperty.Value
    
    # Get files needing review (separated status)
    $needsReview = $batchData.files | Where-Object { $_.status -eq "separated" }
    if ($needsReview.Count -eq 0) {
        Write-Host "No files need review in batch $BatchId" -ForegroundColor Yellow
        return $true
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  BATCH REVIEW: $($batchData.name)" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    Write-Host "Files needing review (separated drums):" -ForegroundColor Yellow
    Write-Host ""
    
    $approvedCount = 0
    
    foreach ($file in $needsReview) {
        Write-Host "[$($file.filename)]" -ForegroundColor White
        Write-Host "  Separation completed: $($file.separation.completed)"
        Write-Host "  Drums file: $($file.separation.drums_path)"
        Write-Host ""
        
        # Simulate tempo detection results (replace with actual BPM detection)
        $detectedBPM = 90 + (Get-Random -Maximum 40)  # Random BPM between 90-130
        $confidence = 60 + (Get-Random -Maximum 35)   # Random confidence 60-95%
        
        Write-Host "  Tempo Detection Results:" -ForegroundColor Cyan
        Write-Host "    BPM: $detectedBPM (Confidence: $confidence%)"
        
        if ($confidence -ge 85) {
            Write-Host "    Status: AUTO-APPROVED (High confidence)" -ForegroundColor Green
            $approved = $true
        } else {
            Write-Host "    Status: NEEDS REVIEW (Low confidence)" -ForegroundColor Yellow
            Write-Host ""
            
            # In a real implementation, this would be interactive
            # For demo, auto-approve 70% of low-confidence files
            $autoApprove = (Get-Random -Maximum 10) -ge 3
            if ($autoApprove) {
                Write-Host "    Human review: APPROVED (simulated)" -ForegroundColor Green
                $approved = $true
            } else {
                Write-Host "    Human review: NEEDS MORE WORK (simulated)" -ForegroundColor Red
                $approved = $false
            }
        }
        
        # Update file status
        for ($i = 0; $i -lt $batchData.files.Count; $i++) {
            if ($batchData.files[$i].id -eq $file.id) {
                if ($approved) {
                    $batchData.files[$i].status = "ready"
                    $batchData.files[$i].review.status = "approved"
                    $batchData.files[$i].review.bpm = $detectedBPM
                    $batchData.files[$i].review.grid_aligned = $true
                    $batchData.files[$i].review.reviewed_at = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                    $batchData.files[$i].review.reviewed_by = "CLI Review System"
                    $approvedCount++
                } else {
                    $batchData.files[$i].status = "separated"  # Keep as separated, needs more review
                    $batchData.files[$i].review.status = "rejected"
                    $batchData.files[$i].review.reviewed_at = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                    $batchData.files[$i].review.reviewed_by = "CLI Review System"
                }
                break
            }
        }
        
        Write-Host ""
        Write-Host "----------------------------------------" -ForegroundColor DarkGray
        Write-Host ""
    }
    
    # Update stats and save
    Update-BatchStats $queue $BatchId
    Save-BatchQueue $queue
    
    Write-Host "Review Summary:" -ForegroundColor Green
    Write-Host "  Files reviewed: $($needsReview.Count)"
    Write-Host "  Approved for processing: $approvedCount"
    Write-Host "  Need additional review: $($needsReview.Count - $approvedCount)"
    Write-Host ""
    
    return $true
}

function Invoke-BatchCommand {
    if (!$BatchAction) {
        Write-Error "Batch action required. Use: create, add, list, status, separate, process, review, delete"
        return $false
    }
    
    try {
        switch ($BatchAction.ToLower()) {
            "create" {
                if (!$BatchName) {
                    Write-Error "Batch name required for create action"
                    return $false
                }
                $result = New-BatchJob -Name $BatchName -Description $Description
                if ($Output -eq "json") {
                    $result | ConvertTo-Json -Depth 4
                } else {
                    Write-Host "Batch created successfully:" -ForegroundColor Green
                    Write-Host "  ID: $($result.batch_id)"
                    Write-Host "  Name: $($result.batch_name)"
                    Write-Host "  Directory: $($result.batch_dir)"
                }
                return $true
            }
            
            "add" {
                Write-SM5Log "Add command - BatchId='$BatchId' BatchName='$BatchName' Input='$Input' InputDir='$InputDir'"
                if (!$BatchId -and !$BatchName) {
                    Write-Error "Batch ID or name required for add action"
                    return $false
                }
                if (!$Input -and !$InputDir) {
                    Write-Error "Input file or directory required for add action"
                    return $false
                }
                
                # Resolve batch ID from name if needed
                if ($BatchName -and !$BatchId) {
                    $queue = Get-BatchQueue
                    foreach ($id in $queue.batches.PSObject.Properties.Name) {
                        if ($queue.batches.$id.name -eq $BatchName) {
                            $BatchId = $id
                            break
                        }
                    }
                    if (!$BatchId) {
                        Write-Error "Batch not found: $BatchName"
                        return $false
                    }
                }
                
                $inputPath = $InputDir
                if ($Input -ne "") { $inputPath = $Input }
                Write-SM5Log "Resolved inputPath: '$inputPath' (Input='$Input', InputDir='$InputDir')"
                $result = Add-FilesToBatch -BatchId $BatchId -InputPath $inputPath -Priority $Priority -Pattern $Pattern
                
                if ($result -and ($Output -eq "json")) {
                    $result | ConvertTo-Json -Depth 4
                } elseif ($result) {
                    Write-Host "Files added successfully:" -ForegroundColor Green
                    Write-Host "  Batch ID: $($result.batch_id)"
                    Write-Host "  Files Added: $($result.files_added)"
                    Write-Host "  Total Files: $($result.total_files)"
                }
                return [bool]$result
            }
            
            "list" {
                $batches = Get-BatchList
                if ($Output -eq "json") {
                    @{
                        platform = "windows"
                        command = "batch list"
                        batches = $batches
                        count = $batches.Count
                        timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
                    } | ConvertTo-Json -Depth 10
                } else {
                    Write-Host "Active Batches:" -ForegroundColor Green
                    Write-Host ""
                    if ($batches.Count -eq 0) {
                        Write-Host "No batches found." -ForegroundColor Yellow
                    } else {
                        foreach ($batch in $batches) {
                            Write-Host "[$($batch.id)]" -ForegroundColor Cyan
                            Write-Host "  Name: $($batch.name)"
                            Write-Host "  Description: $($batch.description)"
                            Write-Host "  Created: $($batch.created)"
                            Write-Host "  Files: $($batch.total_files) (Complete: $($batch.complete), Failed: $($batch.failed))"
                            Write-Host ""
                        }
                    }
                }
                return $true
            }
            
            "status" {
                if (!$BatchId -and !$BatchName) {
                    Write-Error "Batch ID or name required for status action"
                    return $false
                }
                
                # Resolve batch ID from name if needed
                if ($BatchName -and !$BatchId) {
                    $queue = Get-BatchQueue
                    foreach ($id in $queue.batches.PSObject.Properties.Name) {
                        if ($queue.batches.$id.name -eq $BatchName) {
                            $BatchId = $id
                            break
                        }
                    }
                    if (!$BatchId) {
                        Write-Error "Batch not found: $BatchName"
                        return $false
                    }
                }
                
                return Get-BatchStatus -BatchId $BatchId -Watch:$Watch
            }
            
            "separate" {
                if (!$BatchId -and !$BatchName) {
                    Write-Error "Batch ID or name required for separate action"
                    return $false
                }
                
                # Resolve batch ID from name if needed
                if ($BatchName -and !$BatchId) {
                    $queue = Get-BatchQueue
                    foreach ($id in $queue.batches.PSObject.Properties.Name) {
                        if ($queue.batches.$id.name -eq $BatchName) {
                            $BatchId = $id
                            break
                        }
                    }
                    if (!$BatchId) {
                        Write-Error "Batch not found: $BatchName"
                        return $false
                    }
                }
                
                $result = Start-BatchSeparation -BatchId $BatchId -MaxConcurrent $Concurrent
                
                if ($result -and ($Output -eq "json")) {
                    $result | ConvertTo-Json -Depth 4
                } elseif ($result) {
                    Write-Host "Batch separation completed:" -ForegroundColor Green
                    Write-Host "  Batch ID: $($result.batch_id)"
                    Write-Host "  Files Processed: $($result.files_processed)"
                    Write-Host "  Status: $($result.status)"
                }
                return [bool]$result
            }
            
            "process" {
                if (!$BatchId -and !$BatchName) {
                    Write-Error "Batch ID or name required for process action"
                    return $false
                }
                
                # Resolve batch ID from name if needed
                if ($BatchName -and !$BatchId) {
                    $queue = Get-BatchQueue
                    foreach ($id in $queue.batches.PSObject.Properties.Name) {
                        if ($queue.batches.$id.name -eq $BatchName) {
                            $BatchId = $id
                            break
                        }
                    }
                    if (!$BatchId) {
                        Write-Error "Batch not found: $BatchName"
                        return $false
                    }
                }
                
                $result = Start-BatchProcessing -BatchId $BatchId -MaxConcurrent $Concurrent -Engines $Engines
                
                if ($result -and ($Output -eq "json")) {
                    $result | ConvertTo-Json -Depth 4
                } elseif ($result) {
                    Write-Host "Batch processing completed:" -ForegroundColor Green
                    Write-Host "  Batch ID: $($result.batch_id)"
                    Write-Host "  Files Processed: $($result.files_processed)"
                    Write-Host "  Status: $($result.status)"
                }
                return [bool]$result
            }
            
            "review" {
                if (!$BatchId -and !$BatchName) {
                    Write-Error "Batch ID or name required for review action"
                    return $false
                }
                
                # Resolve batch ID from name if needed
                if ($BatchName -and !$BatchId) {
                    $queue = Get-BatchQueue
                    foreach ($id in $queue.batches.PSObject.Properties.Name) {
                        if ($queue.batches.$id.name -eq $BatchName) {
                            $BatchId = $id
                            break
                        }
                    }
                    if (!$BatchId) {
                        Write-Error "Batch not found: $BatchName"
                        return $false
                    }
                }
                
                return Start-BatchReview -BatchId $BatchId
            }
            
            "delete" {
                Write-Host "Batch deletion not yet implemented - coming in next phase!" -ForegroundColor Yellow
                return $true
            }
            
            default {
                Write-Error "Unknown batch action: $BatchAction"
                Write-Host "Available actions: create, add, list, status, separate, process, review, delete"
                return $false
            }
        }
    }
    catch {
        Write-Error "Batch command failed: $($_.Exception.Message)"
        return $false
    }
}

function Get-SM5Status {
    $status = @{
        platform = "windows"
        timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
        SAMPLAR_root = $SAMPLAR_ROOT
        build_dir = $BUILD_DIR
        tests_available = Test-Path $SM5TESTS_EXE
        cmake_configured = Test-Path "$BUILD_DIR\CMakeCache.txt"
    }
    
    if ($Output -eq "json") {
        $status | ConvertTo-Json -Depth 3
    } else {
        Write-Host "SM5CLI Status (Windows)" -ForegroundColor Green
        Write-Host "  SAMPLAR Root: $($status.SAMPLAR_root)"
        Write-Host "  Build Dir: $($status.build_dir)"
        Write-Host "  Tests Available: $($status.tests_available)"
        Write-Host "  CMake Configured: $($status.cmake_configured)"
    }
}

function Invoke-SM5Test {
    Write-SM5Log "Running SAMPLAR tests (category: $Category)"
    
    if (!(Test-Path $SM5TESTS_EXE)) {
        Write-Error "SM5Tests.exe not found. Please build the project first."
        return $false
    }
    
    # Build test command
    $testArgs = @()
    if ($Category) {
        $testArgs += "--category"
        $testArgs += $Category
    }
    if ($VerboseOutput) {
        $testArgs += "--verbose"
    }
    
    $startTime = Get-Date
    
    try {
        # Execute SM5Tests
        Write-SM5Log "Executing: $SM5TESTS_EXE $($testArgs -join ' ')"
        $process = Start-Process -FilePath $SM5TESTS_EXE -ArgumentList $testArgs -NoNewWindow -PassThru -Wait -RedirectStandardOutput "$RESULTS_DIR\test_output_$TIMESTAMP.log" -RedirectStandardError "$RESULTS_DIR\test_error_$TIMESTAMP.log"
        
        $endTime = Get-Date
        $duration = ($endTime - $startTime).TotalMilliseconds
        
        # Parse results (simplified - would need actual SM5Tests output parsing)
        $exitCode = $process.ExitCode
        $success = $exitCode -eq 0
        
        # Create result object
        $result = @{
            platform = "windows"
            command = "test"
            category = if ($Category) { $Category } else { "All" }
            status = if ($success) { "success" } else { "failed" }
            results = @{
                passed = if ($success) { 1 } else { 0 }
                failed = if ($success) { 0 } else { 1 }
                duration_ms = [int]$duration
                exit_code = $exitCode
            }
            timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
            logs = @(
                "$RESULTS_DIR\test_output_$TIMESTAMP.log",
                "$RESULTS_DIR\test_error_$TIMESTAMP.log"
            )
        }
        
        # Output result
        if ($Output -eq "json") {
            $jsonResult = $result | ConvertTo-Json -Depth 4
            $jsonResult | Out-File "$RESULTS_DIR\latest.json" -Encoding UTF8
            Write-Output $jsonResult
        } else {
            Write-Host "Test Results:" -ForegroundColor Green
            Write-Host "  Category: $($result.category)"
            Write-Host "  Status: $($result.status)"
            Write-Host "  Passed: $($result.results.passed)"
            Write-Host "  Failed: $($result.results.failed)"
            Write-Host "  Duration: $($result.results.duration_ms)ms"
        }
        
        return $success
    }
    catch {
        Write-Error "Test execution failed: $($_.Exception.Message)"
        return $false
    }
}

function Invoke-SM5Build {
    Write-SM5Log "Building SAMPLAR (type: $Type)"
    
    $buildType = if ($Type -eq "release") { "Release" } else { "Debug" }
    $buildStartTime = Get-Date
    
    try {
        # CMake configure
        if ($Verify -or !(Test-Path "$BUILD_DIR\CMakeCache.txt")) {
            Write-SM5Log "Configuring CMake..."
            $configArgs = @(
                "-S", ".",
                "-B", "build",
                "-DCMAKE_BUILD_TYPE=$buildType"
            )
            
            & cmake @configArgs
            if ($LASTEXITCODE -ne 0) {
                throw "CMake configuration failed with exit code $LASTEXITCODE"
            }
        }
        
        # Build
        Write-SM5Log "Building project..."
        & cmake --build build --config $buildType
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE"
        }
        
        $buildEndTime = Get-Date
        $buildDuration = ($buildEndTime - $buildStartTime).TotalMilliseconds
        
        # Create result
        $result = @{
            platform = "windows"
            command = "build"
            type = $buildType
            status = "success"
            results = @{
                duration_ms = [int]$buildDuration
                build_dir = $BUILD_DIR
                executables = @(
                    "$BUILD_DIR\$buildType\SAMPLAR.exe",
                    "$BUILD_DIR\$buildType\SM5Tests.exe"
                )
            }
            timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
        }
        
        if ($Output -eq "json") {
            $jsonResult = $result | ConvertTo-Json -Depth 4
            $jsonResult | Out-File "$RESULTS_DIR\build_latest.json" -Encoding UTF8
            Write-Output $jsonResult
        } else {
            Write-Host "Build Results:" -ForegroundColor Green
            Write-Host "  Type: $($result.type)"
            Write-Host "  Status: $($result.status)"
            Write-Host "  Duration: $($result.results.duration_ms)ms"
            Write-Host "  Build Dir: $($result.results.build_dir)"
        }
        
        return $true
    }
    catch {
        Write-Error "Build failed: $($_.Exception.Message)"
        return $false
    }
}

function Invoke-SM5Pipeline {
    Write-SM5Log "Running SAMPLAR pipeline test"
    
    if (!$Input) {
        Write-Error "Pipeline requires --input parameter"
        return $false
    }
    
    if (!(Test-Path $Input)) {
        Write-Error "Input file not found: $Input"
        return $false
    }
    
    $pipelineStartTime = Get-Date
    
    # This would integrate with the actual SAMPLAR pipeline
    # For now, simulate pipeline execution
    Write-SM5Log "Processing: $Input"
    Write-SM5Log "Separation: $Separation"
    Write-SM5Log "Variations: $Variations"
    Write-SM5Log "Engine: $Engine"
    
    # Simulate processing time
    Start-Sleep -Seconds 2
    
    $pipelineEndTime = Get-Date
    $pipelineDuration = ($pipelineEndTime - $pipelineStartTime).TotalMilliseconds
    
    $result = @{
        platform = "windows"
        command = "pipeline"
        input = $Input
        status = "success"
        results = @{
            separation = $Separation.IsPresent
            variations_generated = $Variations
            engine_used = $Engine
            duration_ms = [int]$pipelineDuration
            output_files = @() # Would contain actual output files
        }
        timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    }
    
    if ($Output -eq "json") {
        $jsonResult = $result | ConvertTo-Json -Depth 4
        $jsonResult | Out-File "$RESULTS_DIR\pipeline_latest.json" -Encoding UTF8
        Write-Output $jsonResult
    } else {
        Write-Host "Pipeline Results:" -ForegroundColor Green
        Write-Host "  Input: $($result.input)"
        Write-Host "  Status: $($result.status)"
        Write-Host "  Variations: $($result.results.variations_generated)"
        Write-Host "  Duration: $($result.results.duration_ms)ms"
    }
    
    return $true
}

function Show-SM5Help {
    Write-Host @"
SM5CLI - SAMPLAR Command Line Interface (Windows)

USAGE:
    .\scripts\sm5cli.ps1 <command> [options]

COMMANDS:
    status                          Show SAMPLAR CLI status
    test [options]                  Run tests
    build [options]                 Build project  
    pipeline [options]              Run processing pipeline
    batch <action> [options]        Manage batch processing jobs
    help                           Show this help

TEST OPTIONS:
    -Category <string>             Test category (Core, Services, Engines, etc.)
    -VerboseOutput                 Verbose output
    -Output <json|text>            Output format (default: json)

BUILD OPTIONS:
    -Type <debug|release>          Build type (default: debug)
    -Verify                        Force CMake reconfiguration
    -LogLevel <info|verbose>       Log verbosity

PIPELINE OPTIONS:
    -Input <path>                  Input audio file (required)
    -OutputDir <path>              Output directory (default: tests/results)
    -Separation                    Enable stem separation
    -Variations <number>           Number of variations (default: 5)
    -Engine <string>               Variation engine (default: all)

EXAMPLES:
    .\scripts\sm5cli.ps1 status
    .\scripts\sm5cli.ps1 test -Category Engines -VerboseOutput
    .\scripts\sm5cli.ps1 build -Type release -Verify
    .\scripts\sm5cli.ps1 pipeline -Input test.wav -Separation -Variations 3

BATCH OPTIONS:
    -BatchAction <action>          Action: create, add, list, status, separate, process, review, delete
    -BatchName <string>            Batch name (for create/lookup)
    -BatchId <string>              Batch ID (alternative to name)
    -Description <string>          Batch description (for create)
    -InputDir <path>               Directory to scan for files (for add)
    -Pattern <string>              File patterns (default: *.wav;*.mp3;*.flac;*.aiff)
    -Priority <string>             File priority: normal, high, low (default: normal)
    -Concurrent <number>           Max concurrent jobs (default: 2)
    -Engines <string>              Variation engines (comma-separated)
    -Watch                         Watch mode for status updates

BATCH EXAMPLES:
    .\scripts\sm5cli.ps1 batch -BatchAction create -BatchName "My Album" -Description "Process album tracks"
    .\scripts\sm5cli.ps1 batch -BatchAction add -BatchName "My Album" -InputDir "songs/" -Pattern "*.wav"
    .\scripts\sm5cli.ps1 batch -BatchAction list
    .\scripts\sm5cli.ps1 batch -BatchAction status -BatchName "My Album" -Watch
    .\scripts\sm5cli.ps1 batch -BatchAction separate -BatchId "batch_20260214_172800" -Concurrent 2

"@ -ForegroundColor White
}

# Main execution
switch ($Command) {
    "status" { 
        Get-SM5Status 
    }
    "test" { 
        $success = Invoke-SM5Test
        exit $(if ($success) { 0 } else { 1 })
    }
    "build" { 
        $success = Invoke-SM5Build
        exit $(if ($success) { 0 } else { 1 })
    }
    "pipeline" { 
        $success = Invoke-SM5Pipeline
        exit $(if ($success) { 0 } else { 1 })
    }
    "help" { 
        Show-SM5Help 
    }
    "batch" {
        $success = Invoke-BatchCommand
        exit $(if ($success) { 0 } else { 1 })
    }
    default { 
        Write-Error "Unknown command: $Command"
        Show-SM5Help
        exit 1
    }
}