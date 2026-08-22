# ePaper Clock Development Plan

> Current progress against this plan lives in `docs/STATUS.md`. This document is the
> plan; that document is the truth about where things stand.

## Project Overview
Create an inexpensive, reliable ePaper clock that helps people be on time for church by displaying accurate time with minimal power consumption and optional Home Assistant integration.

## Hardware Components
- 1 x XIAO ePaper Display Board (nRF52840) - EN05
- 1 x 2.9" Monochrome eInk/ePaper Display (296x128 Pixels, SPI interface)
- 1 x Rechargeable LiPo 500mAH battery
- Additional components for 3D printed case

## Development Approach
Systematic, phased development with each phase requiring manual signoff before proceeding:
1. Initial setup and environment validation
2. Display functionality
3. Power management optimization
4. Bluetooth integration
5. Final product refinement

## Phase 1: Initial Setup and Environment Validation
**Goal**: Verify development environment and basic board functionality

### Objectives
- Set up command-line development environment in VS Code on Windows 11
- Install necessary board support packages for XIAO nRF52840
- Choose and record the Arduino core (Adafruit vs Seeed mbed) with rationale
- Create a simple "blinking LED" test application
- Verify board programming and debugging capabilities
- Re-verify the D6 panel power-enable and DC/RST pin aliases on real hardware

### Deliverables
- Working development environment with PlatformIO/VS Code
- Successfully programmed test application
- Documentation of setup process for others to replicate
- Completed `docs/testing/PHASE1_CHECKLIST.md`

## Phase 2: Display Functionality
**Goal**: Implement reliable ePaper display control with partial and full refresh capabilities

### Objectives
- Research and select appropriate ePaper display libraries
- Implement basic text display functionality
- Test different font sizes and layouts
- Determine partial refresh limitations and ghosting behavior
- Establish how many partial refreshes can be performed before requiring a full refresh
- Create a formal checklist for display testing

### Deliverables
- Working ePaper display with configurable text output
- Documentation of refresh behavior and limitations
- Display layout design with time and battery information
- Testing checklist for display validation

## Phase 3: Power Management Optimization
**Goal**: Achieve 3-month battery life with appropriate low battery warnings

### Objectives
- Implement deep sleep modes between display updates
- Optimize refresh intervals (partial refresh every minute, full refresh periodically)
- Implement battery level monitoring and display
- Create low battery warning system
- Measure and optimize overall power consumption
- Test through daylight saving time changes

### Deliverables
- Power-optimized firmware with sleep modes
- Battery monitoring and warning system
- Documentation of power consumption measurements
- Validation through extended testing period (months)

## Phase 4: Bluetooth Integration
**Goal**: Enable time synchronization with Home Assistant using BLE advertising

### Objectives
- Implement BLE advertising for time updates
- Create synchronization protocol with Home Assistant
- Handle connection loss scenarios gracefully
- Implement manual resync button functionality
- Add retry mechanisms for missed updates
- Display data staleness indicators

### Deliverables
- BLE advertising implementation for time sync
- Robust error handling for connection issues
- Manual resync capability
- Documentation of BLE integration

## Phase 5: Final Product Refinement
**Goal**: Complete product with 3D printed case and comprehensive documentation

### Objectives
- Design 3D printed case using OnShape
- Finalize user interface and information display
- Conduct long-term reliability testing (wife as primary tester)
- Create comprehensive documentation for others to build
- Publish project to GitHub with open source license
- Create short video demonstration

### Deliverables
- 3D printed enclosure design files
- Complete source code with documentation
- Build instructions for others
- GitHub repository with all project files
- Video demonstration

## Risk Management
### Technical Risks
- Display refresh limitations or ghosting issues
- Power consumption higher than anticipated
- BLE connectivity problems

### Mitigation Strategies
- Systematic approach with early validation
- Regular AI-assisted documentation of lessons learned
- Flexible approach to change direction if needed

## Documentation Strategy
- Markdown documentation for each phase
- GitHub repository for version control and sharing
- AI-assisted documentation after each development session
- Comprehensive build instructions for others

## Success Criteria
1. Wife successfully uses the clock for months without complaints
2. Battery lasts at least 3 months with low battery warnings
3. Clock maintains accurate time with Home Assistant sync
4. Others can successfully build the clock using provided documentation
5. Project published on GitHub with open source design files

## Timeline
- 2 hours per week dedicated to development
- No strict deadlines, but systematic progress expected
- Each phase requires manual signoff before proceeding