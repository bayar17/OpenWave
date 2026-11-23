.pragma library

function validIpv4(text) {
    const parts = String(text).trim().split(".")
    if (parts.length !== 4)
        return false
    for (const part of parts) {
        if (!/^\d{1,3}$/.test(part) || Number(part) > 255
                || (part.length > 1 && part[0] === "0"))
            return false
    }
    return true
}

function validIpAddress(text) {
    if (validIpv4(text))
        return true
    if (!text.includes(":"))
        return false
    const halves = text.split("::")
    if (halves.length > 2)
        return false
    const groups = []
    for (const half of halves) {
        if (!half.length)
            continue
        for (const group of half.split(":")) {
            if (!group.length)
                return false
            groups.push(group)
        }
    }
    let count = 0
    for (let i = 0; i < groups.length; ++i) {
        if (groups[i].includes(".")) {
            if (i !== groups.length - 1 || !validIpv4(groups[i]))
                return false
            count += 2
        } else {
            if (!/^[0-9a-fA-F]{1,4}$/.test(groups[i]))
                return false
            count++
        }
    }
    return halves.length === 2 ? count < 8 : count === 8
}
