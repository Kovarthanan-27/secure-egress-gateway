package com.sgw;

import jakarta.validation.Valid;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;

/** Dashboard-facing API for managing the policy. */
@RestController
@RequestMapping("/api")
public class RuleController {

    private final PolicyService policy;

    public RuleController(PolicyService policy) {
        this.policy = policy;
    }

    @GetMapping("/rules")
    public List<Dtos.Rule> list() {
        return policy.listRules();
    }

    @PostMapping("/rules")
    public ResponseEntity<Dtos.Rule> create(@Valid @RequestBody Dtos.RuleRequest req) {
        return ResponseEntity.status(HttpStatus.CREATED).body(policy.create(req));
    }

    @PutMapping("/rules/{id}")
    public Dtos.Rule update(@PathVariable long id, @Valid @RequestBody Dtos.RuleRequest req) {
        return policy.update(id, req);
    }

    @DeleteMapping("/rules/{id}")
    public ResponseEntity<Void> delete(@PathVariable long id) {
        policy.delete(id);
        return ResponseEntity.noContent().build();
    }

    @GetMapping("/settings")
    public Dtos.Settings settings() {
        return policy.settings();
    }

    @PutMapping("/settings/default-action")
    public Dtos.Settings setDefaultAction(@Valid @RequestBody Dtos.DefaultActionRequest req) {
        return policy.setDefaultAction(req.action());
    }
}
