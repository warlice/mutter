# Clutter Frame scheduling

`ClutterFrameClock` state diagram.

```mermaid
stateDiagram
    INIT --> SCHEDULED/NOW : start first frame immediately
    IDLE --> SCHEDULED/NOW : given presentation time
    SCHEDULED/NOW --> IDLE : frame clock inhibited or mode changed
    SCHEDULED/NOW --> DISPATCHED_ONE : start a frame
    DISPATCHED_ONE --> IDLE : frame presented or aborted with nothing to draw
    DISPATCHED_ONE --> DISPATCHED_ONE_AND_SCHEDULED/NOW : entering triple buffering
    DISPATCHED_ONE_AND_SCHEDULED/NOW --> SCHEDULED/NOW : frame presented or aborted with nothing to draw
    DISPATCHED_ONE_AND_SCHEDULED/NOW --> DISPATCHED_ONE : frame clock inhibited or mode changed
    DISPATCHED_ONE_AND_SCHEDULED/NOW --> DISPATCHED_TWO : start a second concurrent frame
    DISPATCHED_TWO --> DISPATCHED_ONE : leaving triple buffering
```
