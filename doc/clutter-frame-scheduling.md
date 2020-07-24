# Clutter Frame scheduling

`ClutterFrameClock` state diagram.

```mermaid
stateDiagram
    INIT --> SCHEDULED/SCHEDULED_NOW : start first frame immediately
    IDLE --> SCHEDULED/SCHEDULED_NOW : given presentation time
    SCHEDULED/SCHEDULED_NOW --> IDLE : frame clock inhibited or mode changed
    SCHEDULED/SCHEDULED_NOW --> DISPATCHED : start a frame
    DISPATCHED --> IDLE : frame presented or aborted with nothing to draw
```
